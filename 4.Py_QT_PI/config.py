"""Các tùy chọn dòng lệnh, dùng chung cho Windows demo và Raspberry Pi."""
import argparse
from ipc import default_socket_path


def parse_arguments():
    """Ví dụ: python main.py --demo hoặc --connect --fullscreen."""
    parser = argparse.ArgumentParser(description="Digital Twin telemetry dashboard")
    parser.add_argument("--socket", default=default_socket_path())
    parser.add_argument("--demo", action="store_true")
    parser.add_argument("--connect", action="store_true", help="Connect at startup")
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--stale-seconds", type=float, default=3.0)
    return parser.parse_args()
