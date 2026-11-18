"""One FFmpeg hardware encoder per camera, bounded subscriptions and lifecycle."""
import asyncio
from contextlib import suppress
from collections import deque
import logging
import time
import av
from aiortc import MediaStreamTrack
from aiortc.mediastreams import MediaStreamError
from .h264 import Parser, ffmpeg_command

LOG = logging.getLogger(__name__)


class PacketTrack(MediaStreamTrack):
    kind = "video"

    def __init__(self, source):
        super().__init__()
        self.source = source
        self.queue = asyncio.Queue(maxsize=source.capture.queue_frames)
        self.wait_idr = True
        self.dropped = 0
        self.overflows = 0
        self.wait_started = time.monotonic()
        self.last_idr_wait_ms = None
        source.tracks.add(self)

    def reset(self):
        while not self.queue.empty():
            self.queue.get_nowait()
        self.wait_idr = True

    def push(self, unit):
        if self.readyState != "live":
            return
        if self.queue.full():
            self.overflows += 1
            self.source.queue_overflows += 1
            self.wait_started = time.monotonic()
            self.source.dropped_frames += self.queue.qsize()
            self.dropped += self.queue.qsize()
            self.reset()  # Never resume on a dependent P frame after dropping references.
        if self.wait_idr and not unit.idr:
            self.dropped += 1
            self.source.dropped_frames += 1
            return
        if self.wait_idr:
            self.last_idr_wait_ms = round((time.monotonic() - self.wait_started) * 1000, 1)
        self.wait_idr = False
        self.queue.put_nowait(unit)

    def stats(self):
        return {"queued_frames": self.queue.qsize(), "queue_capacity": self.queue.maxsize,
                "dropped_frames": self.dropped, "queue_overflows": self.overflows,
                "waiting_idr": self.wait_idr, "last_idr_wait_ms": self.last_idr_wait_ms,
                "idr_wait_ms": round((time.monotonic() - self.wait_started) * 1000, 1)
                if self.wait_idr else 0}

    async def recv(self):
        if self.readyState != "live":
            raise MediaStreamError
        try:
            if not self.queue.empty():
                unit = self.queue.get_nowait()
            else:
                unit = await asyncio.wait_for(self.queue.get(), self.source.capture.stale_seconds)
        except asyncio.TimeoutError:
            self.stop()
            raise MediaStreamError from None
        if unit is None or self.readyState != "live":
            raise MediaStreamError
        # An independent Packet for every sender: aiortc packs NALs into RTP,
        # rather than calling its Frame-based libx264 encoder.
        return unit.packet()

    def stop(self):
        if self.readyState == "live":
            self.source.tracks.discard(self)
            self.reset()
            self.queue.put_nowait(None)
        super().stop()


class CameraSource:
    def __init__(self, capture, camera):
        self.capture, self.camera = capture, camera
        self.tracks = set()
        self.task = self.process = None
        self.frames = self.bytes = self.idrs = self.restarts = 0
        self.last_frame = None
        self.queue_overflows = self.dropped_frames = 0
        self._samples = deque()
        self._window_seconds = 5.0
        self.started = time.monotonic()
        self.codec_headers = False
        self.last_error = None
        self._ready = asyncio.Event()

    def start(self):
        if self.task is None:
            self.task = asyncio.create_task(self._run(), name=f"capture:{self.camera.id}")

    def ready(self):
        return (self.codec_headers and self.last_frame is not None
                and time.monotonic() - self.last_frame < self.capture.stale_seconds)

    async def wait_ready(self):
        async with asyncio.timeout(self.capture.startup_timeout):
            while not self.ready():
                self._ready.clear()
                await self._ready.wait()

    def stats(self):
        now = time.monotonic()
        elapsed = max(0.001, now - self.started)
        while self._samples and self._samples[0][0] <= now - self._window_seconds:
            self._samples.popleft()
        window = max(0.001, min(self._window_seconds, now - self.started))
        return {"camera": self.camera.id, "ready": self.ready(), "frames": self.frames,
                "fps_recent": round(len(self._samples) / window, 2),
                "mbps_recent": round(sum(size for _, size in self._samples) * 8 / window / 1e6, 3),
                "queue_overflows": self.queue_overflows, "dropped_frames": self.dropped_frames,
                "tracks": [track.stats() for track in tuple(self.tracks)],
                "fps_average": round(self.frames / elapsed, 2), "idrs": self.idrs,
                "mbps_average": round(self.bytes * 8 / elapsed / 1e6, 3),
                "frame_age_ms": None if self.last_frame is None else round((time.monotonic() - self.last_frame) * 1000, 1),
                "peer_tracks": len(self.tracks), "restarts": self.restarts,
                "last_error": self.last_error}

    async def _stderr(self, process):
        # Drain continuously to prevent pipe deadlock. Camera/codec errors carry
        # no credentials because this process only opens a local /dev/ device.
        while line := await process.stderr.readline():
            LOG.warning("FFmpeg %s: %s", self.camera.id, line.decode(errors="replace").rstrip()[:600])

    async def _terminate(self, process):
        if process.returncode is None:
            with suppress(ProcessLookupError):
                process.terminate()
            try:
                await asyncio.wait_for(process.wait(), 3)
            except asyncio.TimeoutError:
                with suppress(ProcessLookupError):
                    process.kill()
                await process.wait()

    async def _run(self):
        try:
            while True:
                stderr_task = None
                try:
                    parser = Parser(self.camera.fps)
                    self.codec_headers = False
                    self.process = await asyncio.create_subprocess_exec(
                        *ffmpeg_command(self.capture, self.camera),
                        stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
                    stderr_task = asyncio.create_task(self._stderr(self.process))
                    while True:
                        chunk = await asyncio.wait_for(self.process.stdout.read(65536), self.capture.stale_seconds)
                        if not chunk:
                            raise RuntimeError("encoder_exit")
                        for unit in parser.feed(chunk):
                            self.frames += 1
                            self.bytes += len(unit.data)
                            self.idrs += int(unit.idr)
                            self.last_frame = time.monotonic()
                            self._samples.append((self.last_frame, len(unit.data)))
                            while self._samples and self._samples[0][0] <= self.last_frame - self._window_seconds:
                                self._samples.popleft()
                            self.codec_headers = 7 in parser.headers and 8 in parser.headers
                            self.last_error = None
                            self._ready.set()
                            for track in tuple(self.tracks):
                                track.push(unit)
                            # A pipe read may contain several frames. Give RTP senders
                            # a turn before the next frame fills their bounded queues.
                            await asyncio.sleep(0)
                except (OSError, ValueError, RuntimeError, av.FFmpegError, asyncio.TimeoutError) as exc:
                    self.last_error = type(exc).__name__
                    LOG.warning("Camera %s failed (%s); retry in %ss. No software fallback.",
                                self.camera.id, self.last_error, self.capture.retry_seconds)
                finally:
                    self.codec_headers = False
                    # A restart changes the encoder reference chain and timestamp origin.
                    for track in tuple(self.tracks):
                        track.stop()
                    if self.process:
                        await self._terminate(self.process)
                    if stderr_task:
                        stderr_task.cancel()
                        await asyncio.gather(stderr_task, return_exceptions=True)
                    self.process = None
                self.restarts += 1
                await asyncio.sleep(self.capture.retry_seconds)
        finally:
            for track in tuple(self.tracks):
                track.stop()

    async def stop(self):
        if self.task:
            self.task.cancel()
            await asyncio.gather(self.task, return_exceptions=True)
            self.task = None
