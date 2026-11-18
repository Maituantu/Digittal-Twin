# QSS: tương tự CSS, chọn widget bằng tên lớp hoặc objectName (#card, #value...).
# Sửa màu, viền, cỡ chữ ở đây; bố cục màn hình nằm trong screens/dashboard.py.
APP_STYLESHEET = """
        QWidget { background: #f6f8fc; color: #64748b; font-size: 12px; }
        QLabel { background: transparent; }
        QLabel#title { color: #172b4d; font-size: 19px; font-weight: bold; }
        QLabel#badge { font-size: 11px; font-weight: bold; }
        QFrame#card { background: white; border: 1px solid #e2e8f0; border-radius: 12px; }
        QFrame#knobPanel { background: white; border: 1px solid #e2e8f0; border-radius: 12px; }
        QFrame#knobPanel QWidget { background: transparent; }
        QFrame#card QLabel { background: transparent; border: none; }
        QLabel#caption { color: #64748b; font-size: 11px; font-weight: bold; }
        QLabel#value { font-size: 29px; font-weight: bold; }
        QLabel#chartTitle { color: #172b4d; font-size: 14px; font-weight: bold; }
        QLabel#status { color: #334155; }
        QPushButton, QLineEdit { min-height: 36px; border: 1px solid #d8e1ed;
            border-radius: 8px; padding: 0 10px; background: white; color: #334155; }
        QPushButton:hover { background: #eff6ff; border-color: #93b4ee; }
        QPushButton:pressed { background: #dbeafe; }
        QPushButton#primary { background: #eaf2ff; color: #1d4ed8; border-color: #bed4fa; font-weight: bold; }
        QLineEdit:focus { border: 1px solid #2563eb; }
        QWidget:disabled { color: #94a3b8; }
    """
