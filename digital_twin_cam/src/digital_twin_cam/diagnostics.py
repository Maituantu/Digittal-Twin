"""Read-only preflight. An encoder listing is not a hardware benchmark."""
from pathlib import Path
import os
import shutil
import subprocess


def run(command):
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        return result.returncode, result.stdout.strip(), result.stderr.strip()
    except (OSError, subprocess.TimeoutExpired) as exc:
        return 127, "", type(exc).__name__


def doctor(config):
    ok = True
    ffmpeg = shutil.which(config.capture.ffmpeg)
    print(f"FFmpeg: {ffmpeg or 'MISSING'}")
    if ffmpeg:
        rc, stdout, stderr = run([ffmpeg, "-hide_banner", "-encoders"])
        found = "h264_v4l2m2m" in stdout + stderr and rc == 0
        print(f"h264_v4l2m2m listed: {found} (not proof of working hardware)")
        ok &= found
    else:
        ok = False
    for camera in config.cameras:
        if not camera.enabled:
            continue
        present = Path(camera.device).exists()
        access = os.access(camera.device, os.R_OK | os.W_OK)
        print(f"{camera.id}: device={camera.device}, exists={present}, rw_access={access}")
        ok &= present and access
    for label, command in (("Temperature", ["vcgencmd", "measure_temp"]),
                           ("Power/throttle", ["vcgencmd", "get_throttled"])):
        rc, stdout, _ = run(command)
        print(f"{label}: {stdout if rc == 0 else 'unavailable on this host'}")
        if label == "Power/throttle" and rc == 0:
            try:
                active = int(stdout.split("=", 1)[1], 16) & 0xF
                if active:
                    print("Active undervoltage/clock/thermal flags: fix before performance conclusions")
                    ok = False
            except (ValueError, IndexError):
                ok = False
    print("Next: benchmark on Pi; confirm SPS/PPS, IDR cadence, decoding and CPU.")
    return 0 if ok else 1
