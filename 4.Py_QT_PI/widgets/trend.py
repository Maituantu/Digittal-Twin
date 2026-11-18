"""Widget đồ thị từ trường: lưu tối đa 180 mẫu và tự vẽ bằng QPainter."""
from collections import deque
from PyQt5.QtCore import QPointF
from PyQt5.QtGui import QColor, QPainter, QPen, QPolygonF
from PyQt5.QtWidgets import QWidget


class Trend(QWidget):
    """Chỉ nhận số đo để vẽ, không tự đọc socket hoặc tạo dữ liệu demo."""
    def __init__(self):
        super().__init__()
        self.values = deque(maxlen=180)
        self.setMinimumHeight(65)

    def add_sample(self, value):
        """Thêm mẫu mới; deque tự bỏ mẫu cũ nhất khi vượt 180 mẫu."""
        self.values.append(value)
        self.update()

    def clear(self):
        """Xóa lịch sử khi bắt đầu phiên kết nối mới."""
        self.values.clear()
        self.update()

    def paintEvent(self, event):
        """Qt tự gọi khi cần vẽ lại; update() yêu cầu một lần vẽ mới."""
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        rect = self.rect().adjusted(10, 10, -10, -10)
        painter.fillRect(self.rect(), QColor("#ffffff"))
        painter.setPen(QPen(QColor("#e8eef5"), 1))
        for n in range(3):
            y = rect.top() + n * rect.height() / 2
            painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y))
        if len(self.values) < 2:
            return
        low, high = min(self.values), max(self.values)
        # Đổi giá trị đo sang tọa độ pixel; tránh chia cho 0 khi mọi mẫu bằng nhau.
        span = max(high - low, 0.01)
        points = QPolygonF([QPointF(rect.left() + i * rect.width() / (len(self.values) - 1),
                                    rect.bottom() - (value - low) / span * rect.height())
                            for i, value in enumerate(self.values)])
        painter.setPen(QPen(QColor("#0d9488"), 2.5))
        painter.drawPolyline(points)

