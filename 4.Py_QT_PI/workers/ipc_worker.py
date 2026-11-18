"""Luồng nền nhận IPC, giúp thao tác giao diện không bị chặn khi chờ dữ liệu."""
from PyQt5.QtCore import QThread, pyqtSignal
from ipc import IpcReceiver, decode_telemetry


class IpcWorker(QThread):
    """Đọc socket rồi phát signal; chỉ luồng giao diện được sửa widget."""
    # Signal chuyển mẫu đo, trạng thái và số gói lỗi về Dashboard.
    sample = pyqtSignal(object)
    status = pyqtSignal(str)
    invalid_packets = pyqtSignal(int)

    def __init__(self, path):
        super().__init__()
        self.path = path

    def run(self):
        """Qt gọi run() trong luồng nền sau khi Dashboard gọi start()."""
        invalid = 0
        try:
            with IpcReceiver(self.path) as receiver:
                self.status.emit("IPC đã mở · chờ sys_manager")
                while not self.isInterruptionRequested():
                    # receive() có timeout để kiểm tra yêu cầu dừng định kỳ.
                    data = receiver.receive()
                    if data is None:
                        continue
                    try:
                        self.sample.emit(decode_telemetry(data))
                    except (ValueError, UnicodeError):
                        invalid += 1
                    self.invalid_packets.emit(invalid)
        except (OSError, ValueError, RuntimeError) as error:
            self.status.emit(f"Lỗi IPC: {error}")

