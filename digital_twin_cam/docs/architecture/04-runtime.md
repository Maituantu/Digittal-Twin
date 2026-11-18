# Runtime — khởi động, bắt tay và hình xuất hiện

```mermaid
sequenceDiagram
  actor User as Sinh viên
  participant Browser
  participant Server as Web/Auth/Signaling
  participant Pi as Python publisher Pi
  participant FF as FFmpeg + H264 hardware
  Pi->>FF: Mở camera MJPG, giải JPEG, encode H264
  FF-->>Pi: Annex-B; SPS/PPS và IDR
  Pi->>Server: WS publish {kitCode, publishKey}
  Server-->>Pi: published
  Note over Pi,Server: Đây chưa phải xác nhận video đang tới người xem
  User->>Browser: Đăng nhập, vào lượt thực hành hợp lệ, mở camera
  Browser->>Server: subscribe (token, kitCode, attemptId, connectionId)
  Server->>Server: Kiểm tra quyền, kit, lifecycle/MQTT
  Browser->>Server: Offer SDP recvonly
  Server->>Pi: offer {peerId, cameraIndex, sdp, iceServers}
  Pi->>Pi: Chọn camera, tạo PacketTrack, chỉ nhận H264
  Pi->>Server: answer {peerId, sdp}
  Server-->>Browser: Answer SDP
  Pi<<->>Browser: ICE checks -> DTLS -> SRTP keys
  FF-->>Pi: H264 access units liên tục
  Pi->>Browser: RTP/SRTP H264, không encode lại cho mỗi viewer
  Browser->>Browser: Giải mã + hiển thị video
  Browser->>Server: Đóng/thu hồi phiên
  Server->>Pi: close-peer {peerId}
  Pi->>Pi: Stop track, đóng PeerConnection
```

Web chính thức là phần ngoài project, chi tiết thứ tự subscribe/offer phụ thuộc client
đang triển khai. Các message publisher dùng khớp schema đã kiểm tra: `publish`,
`published`, `offer`, `answer`, `close-peer`, `camera-error`. `camera-error` chỉ có
`type` và `peerId`; không thêm `reason` vì backend kiểm tra schema strict.

## Những chỗ dễ nhầm khi đọc log

| Hiện tượng | Đã chứng minh | Chưa chứng minh / việc kiểm tra |
| --- | --- | --- |
| Ping thành công | Có phản hồi ICMP | Cổng WS, khóa, ICE, video chưa chắc OK |
| published | Đúng endpoint/khóa/kit và server chấp nhận publisher | Sinh viên có quyền, kit MQTT online, đã có offer |
| Answer sent | Tạo SDP answer xong | ICE có đường đi, decoder có hình |
| connected | ICE/DTLS đã kết nối | H264 profile/SPS/PPS/IDR đúng, decoded frames tăng |
| Camera FPS tốt | Pi đang xuất access units | Browser không drop, mạng không nghẽn, latency thấp |

## Mất kết nối và dừng

Mất signaling: đóng peers rồi kết nối lại sau khoảng nghỉ cấu hình. Khoảng nghỉ này
khác “nghỉ 5 giây” do luật phân lượt của web. Camera chạy cho đến khi dừng publisher;
đăng ký chờ người xem vẫn có tải encode, chưa làm cơ chế bật/tắt encoder theo demand.

Mất camera/FFmpeg: đóng tracks cũ, dọn child rồi thử mở lại có nghỉ. Không đổi sang CPU
encode. Ctrl+C/SIGTERM: đóng peer, dừng metric task và FFmpeg. Không bật dịch vụ hay
sửa cấu hình boot từ code Python.
