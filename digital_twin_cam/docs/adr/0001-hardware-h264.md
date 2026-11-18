# ADR-0001 — H.264 phần cứng trên Pi3B, giữ signaling web chính thức

Ngày: 2026-10-22. Trạng thái: chọn cho thử nghiệm, chưa nghiệm thu camera thật end-to-end.

## Bối cảnh

Bản cũ chuyển MJPEG từ Pi sang PC, PC encode VP8 và kết nối signaling. Người dùng
muốn thử bỏ PC encode, tận dụng phần cứng H264 của Pi3B, giữ nguyên web chính thức.
UGREEN xuất MJPG/YUYV; không có H264 trong danh sách format đã cung cấp.

## Quyết định

1. USB MJPG → FFmpeg JPEG decode/chuyển YUV420 trên CPU → V4L2 M2M H264 encode.
2. Bắt buộc `h264_v4l2m2m`, không tự fallback `libx264`. Có encoder trong FFmpeg
   chỉ là điều kiện ban đầu; driver mở được và decode được output mới là smoke test.
3. Python/PyAV parse Annex-B và gán PTS; aiortc gửi Packet đã encode qua WebRTC.
4. Một encoder/camera, chia bitstream cho các peer; không khởi tạo hardware encoder
   cho mỗi người xem. Đây là lựa chọn riêng của prototype này, khác bản cũ encode mỗi peer.
5. aiohttp WebSocket giữ publish/offer/answer schema chính thức; không copy web source,
   không giả quyền subscribe/MQTT, không sửa backend đang triển khai.
6. Bắt đầu một camera VGA15; hai camera/30FPS/multiple viewers là từng vòng benchmark.

## Hệ quả

Ít dữ liệu JPEG đi ra mạng và không cần PC encode; Pi vẫn tốn CPU JPEG decode,
đổi màu, RTP, SRTP và tài nguyên USB. Chia bitstream giữ cùng bitrate/profile cho
mọi người; chưa có bitrate adaptation riêng, chưa ép IDR tức thì theo PLI.
Đây không phải bằng chứng “mọi cấu hình H264 đều nhẹ” hoặc “Pi3B chắc chắn gánh hai cam30”.
Nếu các hạn chế làm video mất ổn định, ưu tiên đo và bổ sung điều khiển encoder hoặc
đổi kiến trúc qua ADR mới, không che lỗi bằng CPU fallback.

## Các lựa chọn chưa dùng

- aiortc nhận `av.VideoFrame` rồi chọn H264: mặc định software `libx264`, chưa đạt mục tiêu.
- Camera native H264: thiết bị hiện có không quảng cáo format này.
- Picamera2 trực tiếp: không phải thay thế đơn giản cho pipeline USB UVC hiện tại.
- Media relay trên server: cần tích hợp ngoài phạm vi project, web hiện chỉ signaling.

## Căn cứ kỹ thuật

- [aiortc helpers: MediaPlayer và encoded media](https://aiortc.readthedocs.io/en/latest/helpers.html)
- [aiortc H264: encode Frame khác pack Packet](https://github.com/aiortc/aiortc/blob/1.15.0/src/aiortc/codecs/h264.py)
- [RTCRtpSender chọn encode hay pack](https://github.com/aiortc/aiortc/blob/1.15.0/src/aiortc/rtcrtpsender.py)
- [Raspberry Pi camera software: V4L2 H264 và khác biệt Pi5](https://www.raspberrypi.com/documentation/computers/camera_software.html)

Các nguồn mô tả cơ chế; kết quả thực tế của Pi cụ thể ghi riêng trong verification.
