# Giao diện HDMI cho Raspberry Pi 3

Chép toàn bộ thư mục `4.Py_QT_PI` sang Pi. Dùng Raspberry Pi OS có Desktop
(32-bit hoặc 64-bit). Mở terminal trên màn hình Pi rồi chạy:

```bash
cd ~/4.Py_QT_PI
python3 run.py
```

Nếu thiếu PyQt5, chương trình tự gọi `install_packages.sh` để cài bằng apt.
Lần đầu cần Internet và quyền sudo, có thể hỏi mật khẩu. Các lần sau dùng lại
PyQt5 đã cài. Không cần pip hoặc chép thư viện Windows/venv từ PC sang.
`requirements.txt` ghi lại dependency Python; trên Pi ưu tiên gói apt.

Có thể cài gói trước bằng lệnh:

```bash
bash install_packages.sh
```

Mặc định mở toàn màn hình và kết nối IPC. Xem demo không cần gateway:

```bash
python3 run.py --demo --fullscreen
```

Các tham số được chuyển nguyên sang main.py:

```bash
python3 run.py --connect --fullscreen --stale-seconds 5
python3 run.py --connect --socket /tmp/digital-twin-custom/telemetry.sock
```

F11 bật/tắt toàn màn hình, Escape trở về cửa sổ. Cần phiên Desktop để hiển thị
HDMI; SSH thuần không có display sẽ không mở được giao diện. Không tự cài
Desktop hoặc service khởi động cùng máy. Sau khi cài gói có thể chạy trực tiếp:
`python3 main.py --connect --fullscreen`.

## Nhận dữ liệu thật

Chạy gateway C++ `1.code_pi` trên cùng Pi, bằng cùng user với giao diện.
Thư mục này chỉ chứa UI, không chứa gateway, không trực tiếp đọc UART/MQTT.
Luồng dữ liệu: MCU → UART → sys_manager → Unix socket → PyQt → HDMI.
Hai chương trình có thể khởi động theo thứ tự bất kỳ.

Socket mặc định: `/tmp/digital-twin-<uid>/telemetry.sock`.
Nếu đổi socket, đặt cùng biến `DIGITAL_TWIN_IPC_SOCKET` cho gateway và UI,
hoặc truyền `--socket` tương ứng cho UI. Thư mục cha phải thuộc user hiện tại,
quyền 700. Không chạy UI hoặc chỉ riêng gateway bằng sudo.
Chỉ mở một UI cho mỗi socket. Sau khi chương trình bị kill, chỉ xóa socket cũ
khi đã chắc chắn UI cũ dừng.

## Các file cần chép

- `run.py`: kiểm tra/cài PyQt5 rồi chạy giao diện.
- `install_packages.sh`: cài gói hệ thống trên Pi.
- `main.py`, `config.py`, `ipc.py`, `icons.py`, `theme.py`: mã ứng dụng.
- `models/`, `screens/`, `widgets/`, `workers/`: giữ toàn bộ file `.py`.
- `requirements.txt`, `README.md`: dependency và hướng dẫn.

Đã bỏ `.deps`, cache, tests và ảnh preview khỏi bản triển khai này.