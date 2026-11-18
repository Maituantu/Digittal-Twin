"""Ô thông số dùng lại cho điện áp, dòng điện, từ trường, nguồn, thang đo, thước."""
from PyQt5.QtWidgets import QFrame, QHBoxLayout, QLabel, QVBoxLayout
from icons import icon_pixmap


class MetricCard(QFrame):
    """Chỉ lo hiển thị một thông số; không biết UART, IPC hay dữ liệu demo."""

    def __init__(self, key, title, unit, color, compact=False):
        super().__init__()
        self.unit = unit
        self.default_color = color
        self.setObjectName("card")

        # Hàng trên: icon bên trái, tên thông số bên phải.
        layout = QHBoxLayout(self) if compact else QVBoxLayout(self)
        layout.setContentsMargins(12, 8, 12, 8)
        heading = QHBoxLayout()
        icon = QLabel()
        icon.setPixmap(icon_pixmap(key, color))
        icon.setFixedSize(36, 36)
        heading.addWidget(icon)
        caption = QLabel(title)
        caption.setObjectName("caption")
        heading.addWidget(caption)
        heading.addStretch()
        layout.addLayout(heading)

        # Hàng dưới: giá trị và đơn vị, được cập nhật khi nhận telemetry.
        self.value_label = QLabel("—")
        self.value_label.setObjectName("value")
        layout.addWidget(self.value_label)
        self.set_value_color(color)

    def set_value(self, value):
        """Nhận chuỗi đã định dạng, thêm đơn vị của ô thông số."""
        self.value_label.setText(f"{value} {self.unit}".strip())

    def set_value_color(self, color):
        """Cho phép đổi màu nguồn BẬT/TẮT mà không đổi màu các ô khác."""
        self.value_label.setStyleSheet(f"color: {color};")

    def clear(self):
        """Xóa giá trị cũ khi bắt đầu một phiên kết nối mới."""
        self.value_label.setText("—")
        self.set_value_color(self.default_color)
