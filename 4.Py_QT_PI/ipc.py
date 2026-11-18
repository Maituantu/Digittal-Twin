"""Tầng IPC: mở/đóng Unix socket và giải mã JSON; không chứa mã giao diện Qt."""
from models.telemetry import Telemetry
import json
import math
import os
from pathlib import Path
import socket
import stat




def default_socket_path():
    """Chọn endpoint giống C: ưu tiên biến môi trường, sau đó đường dẫn theo UID."""
    uid = os.getuid() if hasattr(os, "getuid") else 0
    return os.environ.get("DIGITAL_TWIN_IPC_SOCKET") or f"/tmp/digital-twin-{uid}/telemetry.sock"


def decode_telemetry(data):
    """Kiểm tra JSON version 1 và đổi thành Telemetry; báo ValueError nếu sai."""
    try:
        value = json.loads(data)
        if not isinstance(value, dict) or type(value.get("version")) is not int or value["version"] != 1:
            raise ValueError("Unsupported IPC version")
        fields = ("voltage_v", "current_ma", "magnetic_field_mt")
        for name in fields:
            if type(value[name]) not in (float, int) or not math.isfinite(value[name]):
                raise ValueError(f"Invalid {name}")
        for name, limit in (("power_on", 1), ("scale_code", 2), ("ruler_position_mm", 300)):
            if type(value[name]) is not int or not 0 <= value[name] <= limit:
                raise ValueError(f"Invalid {name}")
        return Telemetry(
            voltage_v=value["voltage_v"],
            current_ma=value["current_ma"],
            power_on=value["power_on"],
            magnetic_field_mt=value["magnetic_field_mt"],
            scale_code=value["scale_code"],
            ruler_position_mm=value["ruler_position_mm"],
        )
    except (KeyError, TypeError, OverflowError) as error:
        raise ValueError("Invalid telemetry object") from error


class IpcReceiver:
    """Dùng với `with IpcReceiver(path)` để tự đóng socket và dọn file khi thoát."""
    def __init__(self, path):
        self.path = Path(path)
        self.sock = None
        self.identity = None

    def __enter__(self):
        """Kiểm tra thư mục rồi bind; không xóa socket của một UI khác."""
        if os.name != "posix":
            raise RuntimeError("Unix datagram IPC cần Linux/Pi; dùng --demo trên Windows")
        if not self.path.is_absolute() or len(os.fsencode(self.path)) >= 108:
            raise ValueError("Socket phải là đường dẫn tuyệt đối dưới 108 byte")
        self.path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        info = self.path.parent.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise ValueError("Thư mục IPC phải thuộc user hiện tại và có quyền 700")
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        try:
            self.sock.bind(str(self.path))
            info = self.path.lstat()
            self.identity = (info.st_dev, info.st_ino)
            os.chmod(self.path, 0o600)
            self.sock.settimeout(0.2)
        except Exception:
            self.__exit__(None, None, None)
            raise
        return self

    def receive(self):
        """Nhận một datagram; trả None sau 200 ms nếu chưa có dữ liệu."""
        try:
            return self.sock.recv(4096)
        except socket.timeout:
            return None

    def __exit__(self, *args):
        """Chỉ xóa đúng file socket do receiver này tạo ra."""
        if self.sock:
            self.sock.close()
        if self.identity is not None:
            try:
                info = self.path.lstat()
                if (info.st_dev, info.st_ino) == self.identity:
                    self.path.unlink()
            except FileNotFoundError:
                pass
