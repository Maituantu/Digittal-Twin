"""Manual commands; no service install, remote access or autostart side effects."""
import argparse
import asyncio
from contextlib import suppress
import json
import logging
from pathlib import Path
import signal
import time

from aiortc.mediastreams import MediaStreamError
from .config import load, read_key
from .diagnostics import doctor
from .signaling import Publisher
from .source import CameraSource, PacketTrack

LOG = logging.getLogger(__name__)


async def report(sources):
    while True:
        LOG.info("Capture metrics %s", json.dumps([source.stats() for source in sources.values()]))
        await asyncio.sleep(10)


async def publish(config):
    key = read_key(config.signaling.publish_key_file)
    sources = {c.index: CameraSource(config.capture, c) for c in config.cameras if c.enabled}
    publisher = Publisher(config.signaling, sources, key)
    for source in sources.values():
        source.start()
    metrics = asyncio.create_task(report(sources))
    try:
        await publisher.run()
    finally:
        metrics.cancel()
        await asyncio.gather(metrics, return_exceptions=True)
        await publisher.close()
        await asyncio.gather(*(source.stop() for source in sources.values()))


async def benchmark(config, index, seconds, output):
    camera = next((c for c in config.cameras if c.index == index and c.enabled), None)
    if camera is None:
        raise ValueError("Select an enabled camera index")
    source = CameraSource(config.capture, camera)
    source.start()
    track = None
    received = 0
    started = time.monotonic()
    # Exclusive create prevents accidentally overwriting a previous recording.
    try:
        with Path(output).open("xb") as destination:
            await source.wait_ready()
            track = PacketTrack(source)
            async with asyncio.timeout(seconds + camera.gop / camera.fps + config.capture.stale_seconds):
                first = None
                while first is None or time.monotonic() - first < seconds:
                    packet = await track.recv()
                    if first is None:
                        first = time.monotonic()
                    destination.write(bytes(packet))
                    received += 1
            # timeout is a failure, not a successful benchmark
        result = source.stats()
        result.update(duration_seconds=round(time.monotonic() - started, 2),
                      written_frames=received, output=str(output),
                      encoder="h264_v4l2m2m", bitstream_decode_verified=False,
                      note="Inspect ffprobe/decode output and Pi CPU/power; capture FPS alone is insufficient")
        print(json.dumps(result, indent=2))
    finally:
        if track:
            track.stop()
        await source.stop()


async def interruptible(coroutine):
    task = asyncio.create_task(coroutine)
    loop = asyncio.get_running_loop()
    for signum in (signal.SIGINT, signal.SIGTERM):
        with suppress(NotImplementedError):
            loop.add_signal_handler(signum, task.cancel)
    try:
        return await task
    except asyncio.CancelledError:
        return None
    finally:
        for signum in (signal.SIGINT, signal.SIGTERM):
            with suppress(NotImplementedError):
                loop.remove_signal_handler(signum)


def main():
    parser = argparse.ArgumentParser(description="USB MJPEG -> Pi hardware H264 -> WebRTC browser")
    parser.add_argument("--config", default="config/local.yaml")
    parser.add_argument("--log-level", choices=("DEBUG", "INFO", "WARNING", "ERROR"), default="INFO")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("check", help="Validate config without reading key or opening cameras")
    sub.add_parser("doctor", help="Read-only Pi hardware preflight; no cameras opened")
    sub.add_parser("publish", help="Open enabled cameras and connect official signaling")
    bench = sub.add_parser("benchmark", help="Encode locally without web, signaling or publish key")
    bench.add_argument("--camera-index", type=int, default=0)
    bench.add_argument("--seconds", type=float, default=20)
    bench.add_argument("--output", required=True, help="New .h264 file; existing files are rejected")
    args = parser.parse_args()
    logging.basicConfig(level=args.log_level, format="%(asctime)s %(levelname)s %(name)s %(message)s")
    # Third-party DEBUG logging can contain full SDP/headers. Keep it disabled.
    for name in ("aiortc", "aioice", "aiohttp"):
        logging.getLogger(name).setLevel(logging.WARNING)
    try:
        config = load(args.config)
        if args.command == "check":
            print("Configuration OK; no camera, network or key access attempted")
        elif args.command == "doctor":
            raise SystemExit(doctor(config))
        elif args.command == "publish":
            asyncio.run(interruptible(publish(config)))
        else:
            if not 1 <= args.seconds <= 3600:
                raise ValueError("seconds must be between 1 and 3600")
            asyncio.run(interruptible(benchmark(config, args.camera_index, args.seconds, args.output)))
    except (ValueError, OSError, MediaStreamError, asyncio.TimeoutError) as exc:
        parser.exit(1, f"Failed: {type(exc).__name__}: {exc}\n")
