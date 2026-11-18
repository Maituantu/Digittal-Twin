# Bằng chứng kiểm thử — 22/10/2026

## Trên PC, project độc lập

- Tạo và cài `.venv` riêng bằng pyproject; không dùng import từ repo camera cũ.
- Kiểm thử cấu hình và quyền file khóa; command chỉ dùng `h264_v4l2m2m`.
- Test Annex-B bị cắt thành chunk13byte vẫn tạo đủ15 access units,3IDR và decode đủ15frame.
- Test queue overflow đợi IDR mới, không phát P-frame mất reference; stop đánh thức recv.
- Test hai RTCPeerConnection người nhận thật trên mạng local: SDP chỉ H264, cả hai
  decode hình64x48; phía gửi không tạo context libx264 encode lại.
- Fixture trên PC dùng software libx264 chỉ để tạo mẫu H264; không chứng minh tốc độ phần cứng.
- Lần kiểm tra cuối: 9/9 tests đạt trong môi trường riêng; pip check không có dependency lỗi.
  Có thêm regression cho SPS/PPS thiếu một phần và từ chối SPS level cao hơn SDP.

## Trên Pi thật — smoke test hardware, chưa camera end-to-end

User xác nhận Pi đã cài lại OS và giữ Ethernet10.42.0.55. Khóa SSH được cập nhật
cho riêng host đó sau xác nhận; không bỏ host-key verification, không ghi mật khẩu vào source.
SSH đã đọc được Python3.13.5, aarch64, hai UGREEN `/dev/video0` và `/dev/video2`,
FFmpeg có `h264_v4l2m2m`, capture cũ `digital-twin-camera` active.

Đã chạy ảnh synthetic 640x480/15fps,30frame, qua encoder hardware; không mở camera USB:

```text
Using device /dev/video11
Driver: bcm2835-codec / bcm2835-codec-encode
Encoder: h264_v4l2m2m
FFmpeg exit: 0
ffprobe count_frames: 30
codec_name: h264
profile: Constrained Baseline
level: 31
width/height: 640/480
```

Phát hiện và xử lý trong source mới:

1. `-profile:v baseline` bị FFmpeg V4L2 wrapper từ chối. Dùng mã profile578 cho
   Constrained Baseline, không dùng option văn bản kiểu libx264.
2. `-level:v 31` được nhận nhưng output SPS ban đầu vẫn level40. Thêm bitstream
   filter h264_metadata level3.1 và chặn cấu hình vượt giới hạn Level3.1.
   Phép thử sau đó ffprobe trả profileConstrainedBaseline,level31,30frame giải mã.
3. Parser kiểm tra SPS profile/constraint/level trước khi phát; không gửi bitstream
   khác chuẩn đã thương lượng mà âm thầm cho rằng browser sẽ chấp nhận.

Synthetic chạy nhanh hơn realtime, không tính nó thành FPS camera.
File thử trong thư mục tạm trên Pi đã tự dọn. Không stop/restart/disable capture cũ,
không đăng ký publisher mới, không sửa source/dịch vụ trên web server.

## Điều kiện hiện chưa đạt

Pi báo `throttled=0x50005`: có cờ undervoltage và throttling hiện tại, cùng lịch sử.
Cần xử lý nguồn/cáp trước benchmark hiệu năng dài. Chưa có bằng chứng về:

- USB MJPG camera thật → H264 của project này với FPS/CPU/latency ổn định.
- Hai camera cùng hardware encode hoặc nhiều browser trên Pi3B.
- Publish H264 mới vào phiên sinh viên hợp lệ của web chính thức.
- PLI recovery tức thì, tự thích ứng bitrate cho mạng chậm, public Internet traversal.

Phần project đã sẵn để thực hiện chặngB→C theo benchmark; không đồng nhất smoke
hardware, tests trên PC và nghiệm thu toàn hệ thống.

## Cập nhật: xem web và chuẩn bị 30 FPS

Người dùng xác nhận đã thấy một camera, sau đó cả hai camera trên web chính thức
ở cấu hình15FPS và cảm nhận không lag. Đây là bằng chứng người dùng báo cáo,
chưa phải phép đo decoded FPS/latency từ browser do agent thu thập.

Inspection Pi xác nhận hai FFmpeg encoder đang chạy, cả hai camera bật trong
config/local.yaml. Lấy mẫu khoảng10giây bằng scripts/monitor.py khi publisher15FPS
còn chạy cho kết quả: CPU publisher và các child trung bình93.16% của **một logical
CPU**, peak97.74%; tổng RSS peak214.43MiB (có thể tính trùng shared pages), nhiệt
độ peak53.154°C. Cả9mẫu vẫn có active throttle/power flags; không dùng làm chứng
nhận ổn định lâu dài. Report gốc trên Pi:
`artifacts/web-15fps-baseline-20261022-035211.json`.

Đã lưu cấu hình15FPS trên Pi tại
`config/local.before-30fps-20261022-035222.yaml`, rồi chuẩn bị config/local.yaml
cho cả hai camera640x480/30FPS, GOP30, giữ bitrate1Mbps/camera. Publisher đang chạy
không bị restart nên vẫn dùng15FPS cho đến khi người dùng dừng và chạy lại.
Chưa có kết quả30FPS. Mẫu tham khảo trong repo là config/pi-30fps.example.yaml.

Route đến10.147.19.155 dùng ZeroTier interfaceztiv5oe5ms, source10.147.19.87.
Default route là via10.42.0.1 deveth0: PC hiện vẫn chia sẻ mạng cho Pi. PC không
encode/publish, nhưng chưa thể tắt PC rồi coi Pi vẫn có đường mạng độc lập.

## Chuẩn bị tự kết nối Wi-Fi/ZeroTier sau boot

Đã SSH qua Wi-Fi192.168.88.138 và kiểm tra profile FPT2G có autoconnectyes,
permissionsrỗng và secret psk-flags0. Theo yêu cầu cấu hình boot, đã đặt retries0
và xác nhận NetworkManager/zerotier-one đều enabled,active. Không thay membership,
không cài lại ZeroTier, không restart dịch vụ mạng hoặc reboot để thử.

Đã chuẩn bị hướng dẫn docs/setup-from-scratch.md và template unit đợi
network-online.target/zerotier-one.service. Chưa cài/bật unit camera mới trên Pi;
capture cũ vẫn enabled và cần được chuyển đổi chủ động theo bước7 của hướng dẫn.
Đã chép hướng dẫn và template vào thư mục source trên Pi, không cài vào /etc.
Autostart mạng đã cấu hình; khả năng khôi phục sau reboot thực tế còn phải kiểm tra.

## Kiểm tra gói bàn giao v0.1.0-demo

Bổ sung profile hai camera15FPS, HANDOVER.md, tài liệu cài/systemd và hợp đồng
signaling. Công cụ package dùng allowlist source, phát tar.gz/zip, manifest nội
bộ và checksum archive; không thay đổi pipeline H264 hoặc tiến trình Pi đang chạy.

12tests đạt, gồm kiểm không lọt config local/.env/key/venv/video/cache, đối chiếu
mọi hash tar/zip, từ chối source symlink, và render unit theo user/path của bên
nhận. Unit được kiểm bằng systemd-analyze verify, kể cả đường dẫn có khoảng trắng
và ký tự%. Đã giải nén gói vào thư mục tạm độc lập trên PC, cài .venv mới, pip check,
check cấu hình15FPS và chạy lại12tests thành công; generated unit verify thành công.

Đây là kiểm tra đóng gói/cài đặt trên PC, không phải benchmark hardware mới hoặc
kiểm thử reboot trên Pi nhận. Chưa cài hoặc bật service mới trên Pi trong lần bàn giao.
Bản mặc định bàn giao là15FPS;30FPS vẫn là profile thử nghiệm với giới hạn đã ghi.
