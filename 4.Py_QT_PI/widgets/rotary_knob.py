"""Núm hiển thị có các nấc cố định; không nhận thao tác điều chỉnh."""
import math
from PyQt5.QtCore import Qt, QPointF, QRectF
from PyQt5.QtGui import QColor, QFont, QPainter, QPen
from PyQt5.QtWidgets import QWidget


class RotaryKnob(QWidget):
    """Vẽ các nấc quanh núm; giá trị thực vẫn được giữ nguyên, không làm tròn theo nấc."""

    def __init__(self, values, labels, color, unit=""):
        super().__init__()
        self.values = values
        self.labels = labels
        self.color = color
        self.unit = unit
        self.value = None
        self.setMinimumSize(145, 150)
        self.setFocusPolicy(Qt.NoFocus)
        self.setCursor(Qt.ArrowCursor)
        self.setToolTip("Chỉ hiển thị dữ liệu nhận được, không dùng để điều chỉnh")

    def set_value(self, value):
        self.value = value
        self.update()

    def clear(self):
        self.set_value(None)

    def _position(self):
        """Nội suy vị trí kim khi điện áp đo được nằm giữa hai nấc."""
        if self.value <= self.values[0]:
            return 0
        for index in range(len(self.values) - 1):
            low, high = self.values[index:index + 2]
            if self.value <= high:
                return index + (self.value - low) / (high - low)
        return len(self.values) - 1

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        side = min(self.width(), self.height())
        painter.translate((self.width() - side) / 2, (self.height() - side) / 2)
        painter.scale(side / 180, side / 180)
        painter.setBrush(Qt.NoBrush)
        painter.setPen(QPen(QColor("#e2e8f0"), 5, Qt.SolidLine, Qt.RoundCap))
        painter.drawArc(QRectF(35, 35, 110, 110), -135 * 16, 270 * 16)
        font = QFont(self.font())
        font.setPixelSize(12)
        for index, label in enumerate(self.labels):
            angle = math.radians(135 + index * 270 / (len(self.values) - 1))
            x, y = 90 + 73 * math.cos(angle), 90 + 73 * math.sin(angle)
            painter.setPen(QColor("#475569"))
            painter.setFont(font)
            painter.drawText(QRectF(x - 27, y - 9, 54, 18), Qt.AlignCenter, label)
        painter.setPen(QPen(QColor("#dbe4ef"), 1.5))
        painter.setBrush(QColor("#f8fafc"))
        painter.drawEllipse(QPointF(90, 90), 44, 44)
        if self.value is not None:
            angle = math.radians(135 + self._position() * 270 / (len(self.values) - 1))
            painter.setPen(QPen(QColor(self.color), 4, Qt.SolidLine, Qt.RoundCap))
            painter.drawLine(QPointF(90 + 32 * math.cos(angle), 90 + 32 * math.sin(angle)),
                             QPointF(90 + 42 * math.cos(angle), 90 + 42 * math.sin(angle)))
        font.setPixelSize(21)
        font.setBold(True)
        painter.setFont(font)
        painter.setPen(QColor(self.color))
        if self.value is None:
            text = "—"
        elif self.unit:
            text = f"{self.value:g} {self.unit}"
        else:
            text = self.labels[min(range(len(self.values)), key=lambda i: abs(self.values[i] - self.value))]
        painter.drawText(QRectF(45, 72, 90, 36), Qt.AlignCenter, text)
        painter.end()

