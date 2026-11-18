# C4 Level 1 — System context

## Mục tiêu và ranh giới

Thử thay nhánh Pi JPEG → PC VP8 bằng Pi tự encode H.264 phần cứng, giữ hợp đồng
signaling của web chính thức. PC chỉ dùng lập trình, vận hành/SSH hoặc mở browser;
không còn là nút bắt buộc để encode/chuyển video.

```mermaid
flowchart TB
  Student["Person: sinh viên
Có quyền thực hành"]
  Operator["Person: vận hành
Cấu hình Pi, đọc log, benchmark"]
  Camera["External device: UGREEN USB
Xuất MJPEG / YUYV"]
  System["System: digital_twin_cam
Camera publisher trên Pi3B
Mã hóa H264 phần cứng, WebRTC"]
  Official["External system: web chính thức
UI, tài khoản, lịch, attempt, signaling"]
  Device["External system: thiết bị/MQTT
Heartbeat và lifecycle kit"]
  Student -->|Đăng nhập, mở phiên| Official
  Operator -->|YAML, khóa riêng, CLI/SSH| System
  Camera -->|USB MJPEG| System
  System <-->|WebSocket publish/offer/answer| Official
  System -->|Video WebRTC tới browser của sinh viên| Student
  Device -->|Trạng thái kit, telemetry, attempt_id| Official
```

MQTT/telemetry và quyền xem nằm ngoài project. Một publisher đăng ký thành công
không đủ để kit online/lượt thực hành hợp lệ. Không giả lập dữ liệu để vượt lifecycle.
Server chính thức hiện không chuyển media; biểu diễn UI ở server không có nghĩa
server xử lý các frame. Nếu đổi yêu cầu thành media relay cần ADR và tích hợp riêng.
