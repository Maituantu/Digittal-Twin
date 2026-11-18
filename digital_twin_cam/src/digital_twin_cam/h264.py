"""Annex-B access units; parsing only, no pixel decode or software encoding."""
from dataclasses import dataclass
from fractions import Fraction
import re
import av

TIME_BASE = Fraction(1, 90_000)
START = b"\x00\x00\x00\x01"


def nal_units(data):
    return [part for part in re.split(b"\x00\x00\x00?\x01", data)[1:] if part]


@dataclass(frozen=True)
class AccessUnit:
    data: bytes
    pts: int
    idr: bool

    def packet(self):
        packet = av.Packet(self.data)
        packet.pts = packet.dts = self.pts
        packet.time_base = TIME_BASE
        return packet


class Parser:
    def __init__(self, fps):
        self.codec = av.CodecContext.create("h264", "r")
        self.fps = fps
        self.frames = 0
        self.headers = {}

    def feed(self, chunk):
        output = []
        # CodecContext.parse finds complete access units; decode() is never called.
        for packet in self.codec.parse(chunk):
            data = bytes(packet)
            units = nal_units(data)
            kinds = {unit[0] & 31 for unit in units}
            for unit in units:
                if unit[0] & 31 == 7:
                    if len(unit) < 4 or unit[1] != 66 or not unit[2] & 0x40 or unit[3] > 31:
                        raise ValueError("Expected constrained-baseline H264 at Level <= 3.1")
                if unit[0] & 31 in (7, 8):
                    self.headers[unit[0] & 31] = START + unit
            if not kinds.intersection((1, 5)):
                continue
            idr = 5 in kinds
            # Repeat SPS/PPS on IDRs for late subscribers even if driver emits only once.
            if idr and 7 in self.headers and 8 in self.headers:
                body = [unit for unit in units if unit[0] & 31 not in (7, 8)]
                aud = START + body.pop(0) if body and body[0][0] & 31 == 9 else b""
                data = aud + self.headers[7] + self.headers[8] + b"".join(START + unit for unit in body)
            output.append(AccessUnit(data, self.frames * 90_000 // self.fps, idr))
            self.frames += 1
        return output


def ffmpeg_command(capture, camera):
    # Camera supplies JPEG. FFmpeg decodes JPEG on CPU, converts to YUV420,
    # then requests H.264 encoding from the V4L2 M2M hardware device.
    return [capture.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "warning",
            "-threads", "1", "-thread_queue_size", "2", "-f", "v4l2",
            "-input_format", "mjpeg", "-video_size", f"{camera.width}x{camera.height}",
            "-framerate", str(camera.fps), "-i", camera.device, "-an",
            "-vf", "hflip,vflip,format=yuv420p", "-c:v", "h264_v4l2m2m",
            "-b:v", str(camera.bitrate), "-g", str(camera.gop), "-bf", "0",
            # AV_PROFILE_H264_CONSTRAINED_BASELINE=578, level 3.1=31. V4L2 wrapper
            # on Debian 13 rejects libx264-style text "baseline" / "3.1".
            "-profile:v", "578", "-level:v", "31",
            # Some V4L2 wrappers ignore level; normalize SPS only within the
            # Level 3.1 resolution/rate/bitrate limits enforced by config.py.
            "-bsf:v", "h264_metadata=level=3.1", "-flush_packets", "1",
            "-f", "h264", "pipe:1"]
