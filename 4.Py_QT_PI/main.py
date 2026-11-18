"""Điểm khởi động: đọc cấu hình, áp dụng giao diện và mở màn hình Dashboard."""
import sys
from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import QApplication
from config import parse_arguments
from screens.dashboard import Dashboard
from theme import APP_STYLESHEET


def main():
    """Tạo QApplication trước khi tạo bất kỳ widget hoặc icon nào."""
    args = parse_arguments()
    QApplication.setAttribute(Qt.AA_EnableHighDpiScaling)
    app = QApplication(sys.argv[:1])
    app.setStyle("Fusion")
    # Màu sắc và kiểu chữ được quản lý tập trung trong theme.py.
    app.setStyleSheet(APP_STYLESHEET)
    window = Dashboard(args)
    if args.fullscreen:
        window.showFullScreen()
    else:
        window.show()
    return app.exec_()


if __name__ == "__main__":
    sys.exit(main())
