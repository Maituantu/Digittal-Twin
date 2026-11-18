# Bắt đầu tại đây — gói camera H.264 v0.1.0-demo

Đọc [hướng dẫn cài và systemd](docs/handover.md), sau đó chuyển
[hợp đồng tích hợp](docs/integration-contract.md) cho bên web/backend.

Gói chạy trực tiếp trên Linux Pi, không cần Docker. Mục tiêu đã thử là Pi3B,
camera USB UGREEN MJPG, Python3.13, FFmpeg7.1. Không đảm bảo mọi dòng Pi/OS dùng
cùng encoder phần cứng. Bên nhận kiểm tra driver trước khi nghiệm thu.

```text
USB camera MJPG -> Pi giải JPEG và encode H264 phần cứng -> WebRTC -> browser
                              |                                      |
                              +-------- WebSocket signaling --------+
                                              server web
```

Cài trên Pi sau khi nhận và giải nén gói (không dùng sudo để giải nén/cài .venv):

```bash
tar -xzf digital_twin_cam-0.1.0-demo.tar.gz
cd digital_twin_cam-0.1.0-demo
sha256sum -c MANIFEST.sha256

sudo apt update
sudo apt install -y python3-venv ffmpeg v4l-utils
bash scripts/setup.sh

cp config/pi-15fps.example.yaml config/local.yaml
nano config/local.yaml
.venv/bin/dt-cam --config config/local.yaml check
.venv/bin/dt-cam --config config/local.yaml doctor
```

Chỉ cp tạo config ở lần cài mới; cập nhật bản đang chạy phải giữ config/local.yaml
và khóa bên ngoài. Sửa endpoint, kit được cấp, hai đường camera và file khóa đúng
máy. Ví dụ kit123/server10.147.19.155 không tự cấp quyền cho máy mới; không đăng ký
hai publisher cùng kit. Khóa thật không nằm trong gói; nhận riêng từ người vận hành.

Tạo thư mục nhận khóa trên Pi và đặt quyền sau khi chuyển khóa bằng kênh riêng:

```bash
install -d -m 700 ~/.local/share/digital_twin_cam
# Chuyển khóa chính thức vào publish.key trong thư mục trên.
chmod 600 ~/.local/share/digital_twin_cam/publish.key
.venv/bin/dt-cam --config config/local.yaml publish
```

Chạy tay thành công rồi mới bật [systemd theo hướng dẫn](docs/handover.md#bật-tự-chạy-bằng-systemd).
Profile mặc định bàn giao là hai camera640x480/15FPS/GOP15/1Mbps mỗi camera;
config/pi-30fps.example.yaml dành cho thử tải, không phải cam kết30FPS ổn định.
Khóa, .venv, local.yaml, video, log và lịch sử .git đều không được đóng gói.
