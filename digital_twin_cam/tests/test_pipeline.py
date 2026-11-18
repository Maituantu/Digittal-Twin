"""Software fixture tests validate transport, never Pi hardware throughput."""
import asyncio
from dataclasses import replace
from fractions import Fraction
from pathlib import Path
import tempfile
import sys
import time
import unittest
from unittest.mock import patch
from uuid import uuid4

import av
import yaml
from aiortc import RTCConfiguration, RTCPeerConnection, RTCSessionDescription

from digital_twin_cam.config import Camera, Capture, Signaling, load, read_key
from digital_twin_cam.h264 import AccessUnit, Parser, ffmpeg_command, nal_units
from digital_twin_cam.signaling import Publisher, h264_codecs
from digital_twin_cam.source import CameraSource, PacketTrack


def fixture():
    encoder = av.CodecContext.create("libx264", "w")
    encoder.width, encoder.height = 64, 48
    encoder.pix_fmt = "yuv420p"
    encoder.time_base = Fraction(1, 15)
    encoder.framerate = Fraction(15)
    encoder.options = {"preset": "ultrafast", "tune": "zerolatency", "profile": "baseline",
                       "x264-params": "keyint=5:min-keyint=5:scenecut=0:repeat-headers=1:bframes=0"}
    data = bytearray()
    for i in range(15):
        frame = av.VideoFrame(64, 48, "yuv420p")
        for plane in frame.planes:
            plane.update(bytes([70 + i]) * plane.buffer_size)
        frame.pts = i
        data.extend(b"".join(bytes(p) for p in encoder.encode(frame)))
    data.extend(b"".join(bytes(p) for p in encoder.encode(None)))
    return bytes(data)


class ConfigurationTests(unittest.TestCase):
    def test_example_and_unknown_option(self):
        root = Path(__file__).resolve().parents[1]
        content = (root / "config/pi.example.yaml").read_text()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.yaml"
            path.write_text(content)
            config = load(path)
            self.assertEqual([c.index for c in config.cameras if c.enabled], [0])
            path.write_text(content.replace("fps: 15", "fps_typo: 15"))
            with self.assertRaises(ValueError):
                load(path)

    def test_queue_capacity_validation(self):
        root = Path(__file__).resolve().parents[1]
        content = (root / "config/pi.example.yaml").read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.yaml"
            for value in (0, 31, True, 2.5):
                path.write_text(content.replace("capture:", f"capture:\n  queue_frames: {value}"), encoding="utf-8")
                with self.subTest(value=value), self.assertRaises(ValueError):
                    load(path)
            path.write_text(content.replace("capture:", "capture:\n  queue_frames: 4"), encoding="utf-8")
            self.assertEqual(load(path).capture.queue_frames, 4)

    def test_shared_video_and_legacy_configuration(self):
        root = Path(__file__).resolve().parents[1]
        raw = yaml.safe_load((root / "config/pi-15fps.example.yaml").read_text())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.yaml"
            def read():
                path.write_text(yaml.safe_dump(raw))
                return load(path)
            raw["video"].update(fps=30, width=320, height=240)
            config = read()
            self.assertEqual([(c.fps, c.width, c.height) for c in config.cameras],
                             [(30, 320, 240), (30, 320, 240)])
            raw["cameras"][1]["fps"] = 15
            with self.assertRaisesRegex(ValueError, "Move shared video"):
                read()
            del raw["cameras"][1]["fps"]
            raw["video"]["fps"] = 0
            with self.assertRaises(ValueError):
                read()
            raw["video"]["fps"] = 30
            video = raw.pop("video")
            for camera in raw["cameras"]:
                camera.update(video)
            raw["cameras"][1]["fps"] = 15
            self.assertEqual([c.fps for c in read().cameras], [30, 15])

    def test_secret_permissions(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.key"
            path.write_text("fixture-not-real")
            path.chmod(0o644)
            with self.assertRaises(ValueError):
                read_key(str(path))
            path.chmod(0o600)
            self.assertEqual(read_key(str(path)), "fixture-not-real")

    def test_hardware_command_has_no_software_fallback(self):
        command = ffmpeg_command(Capture(), Camera("one", 0, "/dev/video0"))
        self.assertEqual(command[command.index("-c:v") + 1], "h264_v4l2m2m")
        self.assertNotIn("libx264", command)
        self.assertEqual([c.mimeType for c in h264_codecs()], ["video/H264", "video/H264"])


class BitstreamTests(unittest.TestCase):
    def test_fragmented_annex_b_preserves_access_units_and_decodes(self):
        data = fixture()
        parser = Parser(15)
        units = []
        for i in range(0, len(data), 13):
            units.extend(parser.feed(data[i:i+13]))
        units.extend(parser.feed(b""))
        self.assertEqual(len(units), 15)
        self.assertEqual(sum(unit.idr for unit in units), 3)
        self.assertEqual([u.pts for u in units], list(range(0, 90_000, 6_000)))
        for unit in units:
            if unit.idr:
                self.assertTrue({5, 7, 8}.issubset({n[0] & 31 for n in nal_units(unit.data)}))
        decoder = av.CodecContext.create("h264", "r")
        decoded = [frame for unit in units for frame in decoder.decode(unit.packet())]
        decoded.extend(decoder.decode(None))
        self.assertEqual(len(decoded), 15)
        self.assertEqual((decoded[-1].width, decoded[-1].height), (64, 48))

    def test_partial_parameter_headers_keep_sps_before_pps(self):
        initial = Parser(15)
        units = initial.feed(fixture()) + initial.feed(b"")
        # Driver emits SPS but omits PPS on later IDR: cached PPS must not be
        # inserted before the SPS that a late-joining decoder needs first.
        data = bytearray()
        for index, unit in enumerate(units):
            nals = nal_units(unit.data)
            if index and unit.idr:
                nals = [nal for nal in nals if nal[0] & 31 != 8]
            data.extend(b"".join(b"\x00\x00\x00\x01" + nal for nal in nals))
        parser = Parser(15)
        reparsed = parser.feed(bytes(data)) + parser.feed(b"")
        for unit in reparsed:
            if unit.idr:
                kinds = [nal[0] & 31 for nal in nal_units(unit.data)]
                self.assertLess(kinds.index(7), kinds.index(8))
                self.assertLess(kinds.index(8), kinds.index(5))

    def test_rejects_sps_level_above_negotiated_level(self):
        data = fixture()
        units = nal_units(data)
        sps = next(unit for unit in units if unit[0] & 31 == 7)
        invalid = sps[:3] + bytes([40]) + sps[4:]
        data = data.replace(sps, invalid)
        parser = Parser(15)
        with self.assertRaises(ValueError):
            parser.feed(data)


class TrackTests(unittest.IsolatedAsyncioTestCase):
    async def test_overflow_waits_for_idr_and_stop_wakes_receiver(self):
        source = CameraSource(Capture(), Camera("one", 0, "/dev/video0", fps=4))
        track = PacketTrack(source)
        track.push(AccessUnit(b"initial", 0, True))
        track.push(AccessUnit(b"dependent", 1, False))
        track.push(AccessUnit(b"lost-reference", 2, False))
        self.assertTrue(track.wait_idr)
        self.assertTrue(track.queue.empty())
        track.push(AccessUnit(b"new-idr", 3, True))
        self.assertEqual(bytes(await track.recv()), b"new-idr")
        pending = asyncio.create_task(track.recv())
        await asyncio.sleep(0)
        track.stop()
        from aiortc.mediastreams import MediaStreamError
        with self.assertRaises(MediaStreamError):
            await pending
        self.assertFalse(source.tracks)

    async def test_configured_queue_and_overflow_metrics(self):
        source = CameraSource(Capture(queue_frames=4), Camera("one", 0, "/dev/video0"))
        track = PacketTrack(source)
        try:
            for i in range(4):
                track.push(AccessUnit(b"frame", i, i == 0))
            self.assertEqual(track.queue.qsize(), 4)
            track.push(AccessUnit(b"lost", 4, False))
            self.assertTrue(track.wait_idr)
            self.assertEqual(source.stats()["queue_overflows"], 1)
            self.assertEqual(source.stats()["dropped_frames"], 5)
            self.assertEqual(track.stats()["dropped_frames"], 5)
            track.push(AccessUnit(b"recover", 5, True))
            self.assertEqual(bytes(await track.recv()), b"recover")
            self.assertFalse(track.wait_idr)
            self.assertIsNotNone(track.stats()["last_idr_wait_ms"])
            track.stop()
            self.assertEqual(source.stats()["dropped_frames"], 5)
        finally:
            track.stop()

    async def test_burst_from_pipe_allows_receiver_to_drain(self):
        source = CameraSource(Capture(queue_frames=4), Camera("one", 0, "/dev/video0"))
        track = PacketTrack(source)
        # Real subprocess and pipe, no camera or hardware claims. The parser
        # holds the last access unit until the next frame or EOF.
        command = [sys.executable, "-c",
                   f"import sys,time;sys.stdout.buffer.write({fixture()!r});sys.stdout.buffer.flush();time.sleep(60)"]
        try:
            with patch("digital_twin_cam.source.ffmpeg_command", return_value=command):
                source.start()
                async def drain():
                    for _ in range(14):
                        await track.recv()
                await asyncio.wait_for(drain(), 3)
                self.assertEqual(source.queue_overflows, 0)
                self.assertEqual(track.dropped, 0)
        finally:
            await source.stop()
            track.stop()

    async def test_recent_metrics_expire_when_camera_stalls(self):
        with patch("digital_twin_cam.source.time.monotonic", return_value=100):
            source = CameraSource(Capture(), Camera("one", 0, "/dev/video0"))
        source._samples.extend((100 + i / 30, 1000) for i in range(150))
        with patch("digital_twin_cam.source.time.monotonic", return_value=105):
            self.assertAlmostEqual(source.stats()["fps_recent"], 29.8)
        with patch("digital_twin_cam.source.time.monotonic", return_value=111):
            stats = source.stats()
            self.assertEqual(stats["fps_recent"], 0)
            self.assertEqual(stats["mbps_recent"], 0)

    async def test_source_shutdown_terminates_child_and_stops_tracks(self):
        # Fake process outputs a valid fixture then waits. It validates lifecycle,
        # not camera or hardware encoding, and never runs during real publishing.
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "fixture-process"
            executable.write_text(f"#!{sys.executable}\nimport sys,time\nsys.stdout.buffer.write({fixture()!r})\nsys.stdout.buffer.flush()\ntime.sleep(60)\n")
            executable.chmod(0o700)
            source = CameraSource(Capture(ffmpeg=str(executable), startup_timeout=2),
                                  Camera("one", 0, "/dev/video0"))
            source.start()
            try:
                await source.wait_ready()
                process = source.process
                track = PacketTrack(source)
            finally:
                await source.stop()
            self.assertIsNotNone(process.returncode)
            self.assertEqual(track.readyState, "ended")
            self.assertIsNone(source.process)
            self.assertFalse(source.tracks)

    async def test_two_webrtc_peers_decode_shared_h264_packets(self):
        source = CameraSource(Capture(stale_seconds=3), Camera("one", 0, "/dev/video0"))
        source.codec_headers = True
        source.last_frame = time.monotonic()
        parser = Parser(15)
        units = parser.feed(fixture()) + parser.feed(b"")
        publisher = Publisher(Signaling("ws://localhost/signaling", "test", "/unused"), {0: source}, "test")
        receivers = []
        received = []

        class Socket:
            closed = False
            def __init__(self):
                self.messages = []
            async def send_json(self, message):
                self.messages.append(message)

        async def feed():
            counter = 0
            while True:
                base = units[counter % len(units)]
                unit = replace(base, pts=counter * 6000)
                source.last_frame = time.monotonic()
                for track in tuple(source.tracks):
                    track.push(unit)
                counter += 1
                await asyncio.sleep(1 / 15)

        feeder = asyncio.create_task(feed())
        try:
            for _ in range(2):
                receiver = RTCPeerConnection(RTCConfiguration(iceServers=[]))
                receivers.append(receiver)
                got_track = asyncio.get_running_loop().create_future()
                receiver.on("track", lambda track, target=got_track: target.set_result(track))
                receiver.addTransceiver("video", direction="recvonly")
                await receiver.setLocalDescription(await receiver.createOffer())
                socket = Socket()
                await publisher.negotiate({"peerId": str(uuid4()), "cameraIndex": 0,
                                           "sdp": receiver.localDescription.sdp, "iceServers": []}, socket)
                self.assertEqual(socket.messages[0]["type"], "answer")
                sdp = socket.messages[0]["sdp"]
                self.assertIn("H264/90000", sdp)
                self.assertNotIn("VP8/90000", sdp)
                await receiver.setRemoteDescription(RTCSessionDescription(sdp, "answer"))
                received.append(await asyncio.wait_for(got_track, 3))
            for track in received:
                frame = await asyncio.wait_for(track.recv(), 8)
                self.assertEqual((frame.width, frame.height), (64, 48))
            # The wrapper exists for RTP packetization but no software encoder
            # CodecContext was created on the sending side. Pinned aiortc audit.
            for peer, _ in publisher.peers.values():
                encoder = peer.getSenders()[0]._RTCRtpSender__encoder
                self.assertIsNone(encoder.codec)
        finally:
            feeder.cancel()
            await asyncio.gather(feeder, return_exceptions=True)
            await publisher.close()
            await asyncio.gather(*(receiver.close() for receiver in receivers))
