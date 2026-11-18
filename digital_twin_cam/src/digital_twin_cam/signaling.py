"""Official kit publish/offer/answer contract; WebSocket carries no video."""
import asyncio
import json
import logging
from uuid import UUID
import aiohttp
from aiortc import RTCConfiguration, RTCIceServer, RTCPeerConnection, RTCRtpSender, RTCSessionDescription
from .source import PacketTrack

LOG = logging.getLogger(__name__)


def h264_codecs():
    # Compatible with the constrained-baseline hardware stream. Do not negotiate
    # VP8 or High profile for packets that have already been encoded as baseline.
    return [codec for codec in RTCRtpSender.getCapabilities("video").codecs
            if codec.mimeType.lower() == "video/h264"
            and codec.parameters.get("packetization-mode") == "1"
            and str(codec.parameters.get("profile-level-id", "")).startswith(("4200", "42e0"))]


class Publisher:
    def __init__(self, config, sources, key):
        self.config, self.sources, self.key = config, sources, key
        self.peers = {}
        self.tasks = {}
        self.registered = False

    async def close_peer(self, peer_id):
        task = self.tasks.pop(peer_id, None)
        if task and task is not asyncio.current_task():
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
        entry = self.peers.pop(peer_id, None)
        if entry:
            peer, track = entry
            track.stop()
            await peer.close()

    async def close(self):
        for peer_id in list(set(self.peers) | set(self.tasks)):
            await self.close_peer(peer_id)

    async def negotiate(self, message, ws):
        peer_id = message["peerId"]
        try:
            async with asyncio.timeout(self.config.negotiation_timeout):
                index = message.get("cameraIndex")
                if type(index) is not int or index not in self.sources:
                    raise ValueError("Camera index disabled or invalid")
                source = self.sources[index]
                await source.wait_ready()
                servers = [RTCIceServer(**item) for item in message.get("iceServers", [])]
                peer = RTCPeerConnection(RTCConfiguration(iceServers=servers))
                track = PacketTrack(source)
                self.peers[peer_id] = peer, track

                @track.on("ended")
                async def ended():
                    if self.peers.get(peer_id) == (peer, track):
                        await self.close_peer(peer_id)

                @peer.on("connectionstatechange")
                async def changed():
                    LOG.info("Peer %s camera=%s connection=%s", peer_id, source.camera.id, peer.connectionState)
                    if peer.connectionState in ("failed", "closed"):
                        await self.close_peer(peer_id)

                peer.addTrack(track)
                transceiver = peer.getTransceivers()[0]
                transceiver.direction = "sendonly"
                # Preferences must precede setRemoteDescription: aiortc computes
                # the codec intersection while applying the remote offer.
                transceiver.setCodecPreferences(h264_codecs())
                await peer.setRemoteDescription(RTCSessionDescription(message["sdp"], "offer"))
                transceivers = peer.getTransceivers()
                if (len(transceivers) != 1 or transceivers[0].kind != "video"
                        or peer.sctp is not None):
                    raise ValueError("Expected one receiving video transceiver and no data channel")
                await peer.setLocalDescription(await peer.createAnswer())
                await ws.send_json({"type": "answer", "peerId": peer_id, "sdp": peer.localDescription.sdp})
                LOG.info("Answer H264 sent peer=%s; ICE and decoded frames still need verification", peer_id)
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            # Never log SDP, publishKey, access tokens, or raw signaling messages.
            LOG.warning("Negotiation failed peer=%s class=%s", peer_id, type(exc).__name__)
            await self.close_peer(peer_id)
            if not ws.closed:
                await ws.send_json({"type": "camera-error", "peerId": peer_id})
        finally:
            if self.tasks.get(peer_id) is asyncio.current_task():
                self.tasks.pop(peer_id, None)

    async def dispatch(self, message, ws):
        if not isinstance(message, dict):
            raise ValueError("Expected signaling object")
        kind = message.get("type")
        if kind == "error":
            raise ValueError("Publisher rejected by server")
        if kind not in ("offer", "close-peer"):
            return
        peer_id = message.get("peerId")
        if not isinstance(peer_id, str):
            raise ValueError("Missing peerId")
        UUID(peer_id)
        await self.close_peer(peer_id)
        if kind == "offer":
            if len(set(self.peers) | set(self.tasks)) >= self.config.max_peer_tracks:
                await ws.send_json({"type": "camera-error", "peerId": peer_id})
                return
            self.tasks[peer_id] = asyncio.create_task(self.negotiate(message, ws))

    async def run(self):
        timeout = aiohttp.ClientTimeout(total=None, sock_connect=10)
        async with aiohttp.ClientSession(timeout=timeout) as client:
            while True:
                try:
                    LOG.info("Connecting signaling (credentials hidden)")
                    async with client.ws_connect(self.config.url, heartbeat=15, max_msg_size=131072) as ws:
                        await ws.send_json({"type": "publish", "kitCode": self.config.kit_code,
                                            "publishKey": self.key})
                        first = await ws.receive_json(timeout=10)
                        if not isinstance(first, dict) or first.get("type") != "published":
                            raise ValueError("Publisher registration failed")
                        self.registered = True
                        LOG.info("Publisher registered; waiting for an authorized browser offer")
                        async for packet in ws:
                            if packet.type == aiohttp.WSMsgType.TEXT:
                                await self.dispatch(json.loads(packet.data), ws)
                            elif packet.type == aiohttp.WSMsgType.ERROR:
                                raise ConnectionError("WebSocket failure")
                except (aiohttp.ClientError, OSError, ValueError, TypeError, KeyError,
                        asyncio.TimeoutError) as exc:
                    LOG.warning("Signaling lost class=%s; reconnecting in %ss",
                                type(exc).__name__, self.config.retry_seconds)
                finally:
                    self.registered = False
                    await self.close()
                await asyncio.sleep(self.config.retry_seconds)
