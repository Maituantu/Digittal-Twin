"""Vẽ icon bằng Qt, không cần font biểu tượng hoặc tải ảnh bên ngoài."""
import math
from PyQt5.QtCore import Qt, QPointF, QRectF
from PyQt5.QtGui import QColor, QIcon, QPainter, QPen, QPixmap, QPolygonF


def icon_pixmap(name, color="#2563eb", size=36, background=True):
    """Vẽ icon theo tên thông số trên hệ tọa độ 36×36 rồi đổi về kích thước yêu cầu."""
    pixmap = QPixmap(size, size)
    pixmap.fill(Qt.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.Antialiasing)
    painter.scale(size / 36, size / 36)
    if background:
        tint = QColor(color)
        tint.setAlpha(22)
        painter.setBrush(tint)
        painter.setPen(Qt.NoPen)
        painter.drawRoundedRect(QRectF(0, 0, 36, 36), 10, 10)
    painter.setBrush(Qt.NoBrush)
    painter.setPen(QPen(QColor(color), 1.9, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))

    def line(x1, y1, x2, y2):
        painter.drawLine(QPointF(x1, y1), QPointF(x2, y2))

    if name == "voltage":
        painter.drawPolyline(QPolygonF([QPointF(20, 7), QPointF(11, 20), QPointF(18, 20),
                                        QPointF(16, 29), QPointF(25, 15), QPointF(18, 15), QPointF(20, 7)]))
    elif name == "current":
        painter.drawPolyline(QPolygonF([QPointF(7 + i, 18 - 6 * math.sin(i * math.pi / 10))
                                        for i in range(23)]))
    elif name == "field":
        painter.drawArc(QRectF(9, 10, 18, 18), 180 * 16, 180 * 16)
        line(9, 19, 9, 9)
        line(27, 19, 27, 9)
        line(9, 14, 14, 14)
        line(22, 14, 27, 14)
        line(14, 14, 14, 19)
        line(22, 14, 22, 19)
        painter.drawArc(QRectF(14, 15, 8, 8), 180 * 16, 180 * 16)
    elif name == "power":
        painter.drawArc(QRectF(9, 9, 18, 18), 130 * 16, 280 * 16)
        line(18, 7, 18, 18)
    elif name == "scale":
        painter.drawArc(QRectF(7, 9, 22, 22), 0, 180 * 16)
        line(9, 25, 27, 25)
        line(18, 23, 24, 15)
        for x, y in [(10, 17), (18, 12), (26, 17)]:
            painter.drawEllipse(QPointF(x, y), .7, .7)
    elif name == "ruler":
        painter.drawRoundedRect(QRectF(7, 12, 22, 13), 2, 2)
        for i in range(5):
            line(11 + i * 4, 12, 11 + i * 4, 19 if i % 2 == 0 else 17)
    elif name == "connect":
        painter.drawRoundedRect(QRectF(11, 13, 14, 11), 3, 3)
        line(14, 8, 14, 13)
        line(22, 8, 22, 13)
        line(18, 24, 18, 29)
    elif name == "fullscreen":
        for x, y, dx, dy in [(8, 8, 7, 7), (28, 8, -7, 7), (8, 28, 7, -7), (28, 28, -7, -7)]:
            line(x, y, x + dx, y)
            line(x, y, x, y + dy)
    painter.end()
    return pixmap


def button_icon(name):
    """Tạo QIcon kích thước 24 px, nền trong suốt để gắn lên nút bấm."""
    return QIcon(icon_pixmap(name, size=24, background=False))
