"""Màn hình chính: ghép các widget, quản lý kết nối và cập nhật telemetry."""
import math
import time
from PyQt5.QtCore import Qt, QTimer
from PyQt5.QtWidgets import (QLineEdit, QFrame, QGridLayout, QHBoxLayout, QLabel,
                             QMainWindow, QPushButton, QSizePolicy, QVBoxLayout, QWidget)
from models.telemetry import Telemetry
from icons import button_icon
from widgets.metric_card import MetricCard
from widgets.trend import Trend
from widgets.rotary_knob import RotaryKnob
from workers.ipc_worker import IpcWorker


class Dashboard(QMainWindow):
    """Bên trái: núm, nguồn, thước. Bên phải: các giá trị đo và đồ thị."""
    def __init__(self, args):
        super().__init__()
        self.args = args
        self.worker = None  # Luồng nhận IPC; None khi chưa kết nối.
        self.last_sample = None
        self.count = 0
        self.started = time.monotonic()
        self.setWindowTitle("Digital Twin · IPC Telemetry")
        self.resize(800, 480)
        root = QWidget()
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)
        layout.setContentsMargins(16, 10, 16, 10)
        layout.setSpacing(8)
        self._build_header(layout)
        self._build_connection_bar(layout)
        self._build_measurements(layout)
        self._build_status_bar(layout)
        self.invalid_count = 0
        # Chuỗi lỗi dài không được ép cửa sổ rộng hơn màn hình HDMI.
        for label in (self.status, self.footer):
            label.setMinimumWidth(0)
            label.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        self.timer = QTimer(self)
        # Mỗi 200 ms: cập nhật demo, trạng thái và cảnh báo dữ liệu cũ.
        self.timer.timeout.connect(self.tick)
        self.timer.start(200)
        if args.demo:
            for widget in (self.endpoint, self.connect_button):
                widget.setEnabled(False)
        elif args.connect:
            QTimer.singleShot(0, self.toggle_connection)

    def _build_header(self, layout):
        """Phần 1: tên màn hình và nhãn trạng thái ở góc trên phải."""
        header = QHBoxLayout()
        title = QLabel("Digital Twin  /  Giám sát thiết bị")
        title.setObjectName("title")
        title.setMinimumWidth(0)
        title.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        header.addWidget(title, 1)
        self.mode = QLabel("DEMO · dữ liệu mô phỏng" if self.args.demo else "IPC · dữ liệu thực")
        self.mode.setObjectName("badge")
        self.mode.setFixedWidth(155)
        self.mode.setAlignment(Qt.AlignRight | Qt.AlignVCenter)
        header.addWidget(self.mode)
        layout.addLayout(header)

    def _build_connection_bar(self, layout):
        """Phần 2: đường dẫn IPC, nút kết nối và nút toàn màn hình."""
        controls = QHBoxLayout()
        self.endpoint = QLineEdit(self.args.socket)
        self.endpoint.setMinimumWidth(80)
        self.endpoint.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Fixed)
        self.connect_button = QPushButton("Kết nối")
        self.connect_button.setIcon(button_icon("connect"))
        self.connect_button.setObjectName("primary")
        self.connect_button.clicked.connect(self.toggle_connection)
        controls.addWidget(self.endpoint, 3)
        controls.addWidget(self.connect_button)
        full = QPushButton("Toàn màn hình")
        full.setIcon(button_icon("fullscreen"))
        full.clicked.connect(self.toggle_fullscreen)
        controls.addWidget(full)
        layout.addLayout(controls)

    def _build_measurements(self, layout):
        """Phần 3: chia vùng trái/phải; các núm chỉ hiển thị dữ liệu."""
        body = QHBoxLayout()
        body.setSpacing(12)
        left = QVBoxLayout()
        left.setSpacing(8)
        left_title = QLabel("TRẠNG THÁI THIẾT BỊ")
        left_title.setObjectName("chartTitle")
        left.addWidget(left_title)
        knobs = QFrame()
        knobs.setObjectName("knobPanel")
        knob_layout = QHBoxLayout(knobs)
        knob_layout.setContentsMargins(6, 8, 6, 4)
        self.voltage_knob = RotaryKnob([0, 3, 6, 8, 12], ["0", "3", "6", "8", "12"],
                                       "#d18a06", "V")
        self.scale_knob = RotaryKnob([0, 1, 2], ["1.999", "19.99", "199.9"],
                                     "#7c3aed")
        for title, knob in (("ĐIỆN ÁP · V", self.voltage_knob), ("THANG ĐO", self.scale_knob)):
            column = QVBoxLayout()
            label = QLabel(title)
            label.setAlignment(Qt.AlignCenter)
            label.setObjectName("caption")
            column.addWidget(label)
            column.addWidget(knob, 1)
            knob_layout.addLayout(column, 1)
        left.addWidget(knobs, 1)
        hint = QLabel("DEMO: hiển thị dữ liệu mô phỏng" if self.args.demo else "Chỉ hiển thị dữ liệu nhận từ IPC")
        left.addWidget(hint)
        bottom = QHBoxLayout()
        self.cards = {}
        for key, title, unit, color in [("power", "NGUỒN", "", "#16a34a"),
                                        ("ruler", "VỊ TRÍ THƯỚC", "mm", "#ea580c")]:
            card = MetricCard(key, title, unit, color)
            self.cards[key] = card
            bottom.addWidget(card, 1)
        left.addLayout(bottom)
        right = QVBoxLayout()
        right.setSpacing(8)
        title = QLabel("THÔNG SỐ ĐO TRỰC TIẾP")
        title.setObjectName("chartTitle")
        right.addWidget(title)
        for key, title, unit, color in [("voltage", "ĐIỆN ÁP", "V", "#d18a06"),
                                        ("current", "DÒNG ĐIỆN", "mA", "#2563eb"),
                                        ("field", "TỪ TRƯỜNG", "mT", "#0d9488")]:
            card = MetricCard(key, title, unit, color, compact=True)
            self.cards[key] = card
            right.addWidget(card, 1)
        self._build_chart(right)
        body.addLayout(left, 1)
        body.addLayout(right, 1)
        layout.addLayout(body, 1)

    def _build_chart(self, layout):
        """Phần 4: tiêu đề đồ thị và widget Trend."""
        chart_header = QHBoxLayout()
        chart_title = QLabel("Diễn biến từ trường")
        chart_title.setObjectName("chartTitle")
        chart_header.addWidget(chart_title)
        chart_header.addStretch()
        chart_header.addWidget(QLabel("mT · 180 mẫu"))
        layout.addLayout(chart_header)
        self.trend = Trend()
        layout.addWidget(self.trend, 1)

    def _build_status_bar(self, layout):
        """Phần 5: trạng thái chi tiết, tổng mẫu nhận và số gói IPC lỗi."""
        self.status = QLabel("Chưa kết nối")
        self.status.setObjectName("status")
        layout.addWidget(self.status)
        self.footer = QLabel("0 mẫu  |  Gói IPC lỗi: 0")
        layout.addWidget(self.footer)

    def toggle_connection(self):
        """Nút kết nối: yêu cầu dừng luồng đang chạy hoặc tạo phiên nhận mới."""
        if self.worker:
            self.worker.requestInterruption()
            self.connect_button.setEnabled(False)
            return
        self.last_sample = None
        self.count = 0
        self.invalid_count = 0
        self.trend.clear()
        self.voltage_knob.clear()
        self.scale_knob.clear()
        for card in self.cards.values():
            card.clear()
        self.worker = IpcWorker(self.endpoint.text())
        # Signal từ luồng nền được Qt chuyển về luồng giao diện.
        self.worker.sample.connect(self.receive)
        self.worker.status.connect(self.status.setText)
        self.worker.invalid_packets.connect(self.set_invalid_count)
        self.worker.finished.connect(self.disconnected)
        self.endpoint.setEnabled(False)
        self.connect_button.setText("Ngắt kết nối")
        self.worker.start()

    def disconnected(self):
        """Chỉ giải phóng worker sau khi luồng nền đã kết thúc."""
        self.worker.deleteLater()
        self.worker = None
        self.endpoint.setEnabled(True)
        self.connect_button.setEnabled(True)
        self.connect_button.setText("Kết nối")
        if not self.status.text().startswith("Lỗi IPC"):
            self.status.setText("Đã ngắt kết nối · giá trị cuối được giữ lại")

    def set_invalid_count(self, invalid):
        """Nhận số gói JSON lỗi từ worker, không liên quan đến CRC UART."""
        self.invalid_count = invalid

    def receive(self, sample):
        """Cập nhật các ô và đồ thị từ một mẫu Telemetry hợp lệ."""
        self.last_sample = time.monotonic()
        self.count += 1
        values = {"voltage": f"{sample.voltage_v:.2f}", "current": f"{sample.current_ma:.2f}",
                  "field": f"{sample.magnetic_field_mt:.4f}",
                  "power": "BẬT" if sample.power_on else "TẮT",
                  "ruler": str(sample.ruler_position_mm)}
        self.voltage_knob.set_value(sample.voltage_v)
        self.scale_knob.set_value(sample.scale_code)
        for key, value in values.items():
            self.cards[key].set_value(value)
        self.cards["power"].set_value_color("#16a34a" if sample.power_on else "#64748b")
        self.trend.add_sample(sample.magnetic_field_mt)

    def tick(self):
        """Xử lý định kỳ trên luồng giao diện, không thực hiện đọc socket."""
        if self.args.demo:
            t = time.monotonic() - self.started
            self.receive(Telemetry(6, 125 + math.sin(t / 2) * 10,
                                   1, .75 + math.sin(t) * .15, 1,
                                   int(150 + 50 * math.sin(t / 3))))
            self.status.setText("DEMO · dữ liệu mô phỏng, không kết nối IPC")
        elif self.worker and self.last_sample is not None:
            age = time.monotonic() - self.last_sample
            self.status.setText(f"{'MẤT DỮ LIỆU' if age > self.args.stale_seconds else 'Đang nhận'} · mẫu cuối {age:.1f}s trước")
        if self.args.demo:
            badge, color, tint = "●  DEMO", "#a16207", "#fef9c3"
        elif self.worker and self.last_sample is not None and time.monotonic() - self.last_sample <= self.args.stale_seconds:
            badge, color, tint = "●  ĐANG NHẬN", "#15803d", "#dcfce7"
        elif self.worker and self.last_sample is None:
            badge, color, tint = "●  CHỜ DỮ LIỆU", "#a16207", "#fef9c3"
        else:
            badge, color, tint = "●  MẤT DỮ LIỆU" if self.worker else "●  CHƯA KẾT NỐI", "#b91c1c", "#fee2e2"
        self.mode.setText(badge)
        self.mode.setStyleSheet(f"color: {color}; background: {tint}; border-radius: 8px; padding: 5px 10px;")
        self.footer.setText(f"{self.count} mẫu đã nhận  |  Gói IPC lỗi: {self.invalid_count}")

    def toggle_fullscreen(self):
        """Dùng chung cho nút toàn màn hình và phím F11."""
        if self.isFullScreen():
            self.showNormal()
        else:
            self.showFullScreen()

    def keyPressEvent(self, event):
        """Xử lý Escape/F11; chuyển các phím còn lại cho Qt."""
        if event.key() == Qt.Key_Escape:
            self.showNormal()
        elif event.key() == Qt.Key_F11:
            self.toggle_fullscreen()
        else:
            super().keyPressEvent(event)

    def closeEvent(self, event):
        """Dừng và chờ worker đóng socket trước khi đóng cửa sổ."""
        if self.worker:
            self.worker.requestInterruption()
            self.worker.wait()
        event.accept()

