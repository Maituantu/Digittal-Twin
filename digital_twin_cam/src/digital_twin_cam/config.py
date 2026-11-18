"""Validated, immutable configuration; secrets stay in a separate file."""
from dataclasses import dataclass, fields
from pathlib import Path
from urllib.parse import urlsplit
import stat
import math
import yaml


@dataclass(frozen=True)
class Video:
    """Shared encoding settings, applied identically to every camera."""
    width: int = 640
    height: int = 480
    fps: int = 15
    bitrate: int = 1_000_000
    gop: int = 15


@dataclass(frozen=True)
class Camera:
    id: str
    index: int
    device: str
    width: int = 640
    height: int = 480
    fps: int = 15
    bitrate: int = 1_000_000
    gop: int = 15
    enabled: bool = True


@dataclass(frozen=True)
class Capture:
    queue_frames: int = 2
    ffmpeg: str = "ffmpeg"
    startup_timeout: float = 15
    stale_seconds: float = 5
    retry_seconds: float = 3


@dataclass(frozen=True)
class Signaling:
    url: str
    kit_code: str
    publish_key_file: str
    max_peer_tracks: int = 4
    negotiation_timeout: float = 20
    retry_seconds: float = 3


@dataclass(frozen=True)
class Config:
    signaling: Signaling
    capture: Capture
    cameras: tuple[Camera, ...]


def construct(cls, value):
    if not isinstance(value, dict):
        raise ValueError(f"{cls.__name__} must be a mapping")
    unknown = set(value) - {field.name for field in fields(cls)}
    if unknown:
        raise ValueError(f"Unknown {cls.__name__} fields: {sorted(unknown)}")
    try:
        return cls(**value)
    except TypeError as exc:
        raise ValueError(f"Invalid {cls.__name__} fields") from exc


def positive(value, name, maximum=None):
    if type(value) not in (int, float) or not math.isfinite(value) or value <= 0 or (maximum and value > maximum):
        raise ValueError(f"{name} must be positive" + (f" and <= {maximum}" if maximum else ""))


def load(path):
    raw = yaml.safe_load(Path(path).read_text())
    required = {"signaling", "capture", "cameras"}
    if not isinstance(raw, dict) or not required <= set(raw) or set(raw) - required - {"video"}:
        raise ValueError("Expected signaling, capture, cameras and optional video sections only")
    shared = None
    if "video" in raw:
        video = construct(Video, raw["video"])
        shared = {field.name: getattr(video, field.name) for field in fields(Video)}
    signaling = construct(Signaling, raw["signaling"])
    capture = construct(Capture, raw["capture"])
    url = urlsplit(signaling.url)
    if url.scheme not in ("ws", "wss") or not url.hostname or url.username or url.password or url.fragment:
        raise ValueError("Use ws:// or wss:// without embedded credentials or fragment")
    if not isinstance(signaling.kit_code, str) or not signaling.kit_code.strip():
        raise ValueError('kit_code must be a nonempty string, e.g. "123"')
    if not isinstance(signaling.publish_key_file, str) or not signaling.publish_key_file:
        raise ValueError("publish_key_file is required")
    for name in ("max_peer_tracks", "negotiation_timeout", "retry_seconds"):
        positive(getattr(signaling, name), name)
    if type(signaling.max_peer_tracks) is not int:
        raise ValueError("max_peer_tracks must be an integer")
    if not isinstance(capture.ffmpeg, str) or not capture.ffmpeg:
        raise ValueError("ffmpeg executable is required")
    for name in ("startup_timeout", "stale_seconds", "retry_seconds"):
        positive(getattr(capture, name), name)
    if not isinstance(raw["cameras"], list) or not raw["cameras"]:
        raise ValueError("cameras must be a nonempty list")
    if type(capture.queue_frames) is not int or not 1 <= capture.queue_frames <= 30:
        raise ValueError("queue_frames must be an integer between 1 and 30")
    camera_items = []
    for item in raw["cameras"]:
        if not isinstance(item, dict):
            raise ValueError("Camera must be a mapping")
        if shared is not None:
            duplicate = set(item) & set(shared)
            if duplicate:
                raise ValueError(f"Move shared video fields out of cameras into video: {sorted(duplicate)}")
            item = {**item, **shared}
        camera_items.append(construct(Camera, item))
    cameras = tuple(camera_items)
    if len({c.id for c in cameras}) != len(cameras) or len({c.index for c in cameras}) != len(cameras):
        raise ValueError("Duplicate camera id/index")
    if not any(c.enabled for c in cameras):
        raise ValueError("Enable at least one camera")
    for camera in cameras:
        if not isinstance(camera.id, str) or not camera.id or len(camera.id) > 64:
            raise ValueError("Invalid camera id")
        if type(camera.index) is not int or camera.index not in (0, 1):
            raise ValueError("Official signaling supports camera index 0 or 1")
        if type(camera.enabled) is not bool or not isinstance(camera.device, str) or not camera.device.startswith("/dev/"):
            raise ValueError("Use enabled: true/false and an absolute /dev/ camera path")
        for name, maximum in (("width", 1920), ("height", 1080), ("fps", 60), ("bitrate", 25_000_000), ("gop", 300)):
            value = getattr(camera, name)
            positive(value, name, maximum)
            if type(value) is not int:
                raise ValueError(f"{name} must be an integer")
        macroblocks = ((camera.width + 15) // 16) * ((camera.height + 15) // 16)
        if macroblocks > 3600 or macroblocks * camera.fps > 108000 or camera.bitrate > 14_000_000:
            raise ValueError("Camera exceeds H264 Baseline Level 3.1 limits")
        if camera.width % 2 or camera.height % 2:
            raise ValueError("YUV420 requires even dimensions")
    return Config(signaling, capture, cameras)


def read_key(path):
    file = Path(path).expanduser()
    if not file.is_absolute():
        raise ValueError("Publish key path must be absolute (or use ~)")
    if stat.S_IMODE(file.stat().st_mode) & 0o077:
        raise ValueError("Publish key must be private: chmod 600 <key-file>")
    key = file.read_text().strip()
    if not key:
        raise ValueError("Publish key is empty")
    return key
