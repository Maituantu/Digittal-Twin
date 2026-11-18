# Hợp đồng ghép camera — H.264 publisher v0.1.0-demo

Tài liệu này mô tả client hiện có trong src/digital_twin_cam/signaling.py;
không phải schema để tự thay đổi backend chính thức đang chạy.

## Ranh giới trách nhiệm

| Bên | Trách nhiệm |
| --- | --- |
| Pi publisher | USB MJPG, giải JPEG CPU, encode H264 phần cứng, trả SDP answer, gửi video |
| Server/backend | Cấp/kiểm khóa publish, kit, quyền subscribe, attempt; chuyển signaling; cung cấp ICE config |
| Browser/frontend | Đăng nhập, xin quyền/lượt hợp lệ, tạo offer recvonly, nhận và hiển thị H264 |
| Thiết bị/MQTT riêng | Heartbeat, telemetry, attempt_id theo hợp đồng backend; không nằm trong publisher camera |

PC trung gian không tham gia encode/publish. Server hiện chỉ auth/signaling,
không nhận/relay media. Nếu cần Pi→server→browser phải phát triển nhánh media relay
riêng và chốt hợp đồng mới, không chỉ đổi URL hoặc cổng.

## Kết nối và đăng ký

Bên backend cấp:

- WebSocket URL: ví dụ ws://10.147.19.155/signaling cho mạng riêng, WSS khi public.
- kitCode đã đăng ký và được phép phát; ví dụ123 chỉ là giá trị của môi trường thử.
- publishKey chính thức; cấp bằng kênh riêng, lưu file0600 ngoài repo trên Pi.
- STUN/TURN/ICE config theo hạ tầng. Truy cập UI không chứng minh media tới được Pi.

Pi mở WebSocket và gửi ngay:

```json
{"type":"publish","kitCode":"123","publishKey":"<KEY_FROM_OPERATOR>"}
```

Server xác nhận:

```json
{"type":"published"}
```

Chưa có người xem thì publisher chờ offer, camera vẫn capture/encode. published
không đồng nghĩa kit/MQTT online, có lượt hợp lệ, ICE connected hay browser có hình.

## Một peer cho một camera trong một phiên xem

Browser xin subscribe theo hệ thống chính thức: accessToken, kitCode, attemptId,
connectionId hợp lệ. Client Pi không nhận token sinh viên và không tạo giả lượt.
Server chuyển offer của browser tới Pi:

```json
{
  "type":"offer",
  "peerId":"11111111-1111-4111-8111-111111111111",
  "cameraIndex":0,
  "sdp":"<COMPLETE_RECVONLY_VIDEO_OFFER_SDP>",
  "iceServers":[]
}
```

- peerId là UUID; cameraIndex là số nguyên0 hoặc1, không phải tên /dev/videoN.
- Một offer chỉ có một video transceiver; không audio/data channel.
- Browser offer cần H264 phù hợp, packetization-mode1, profile Baseline tương thích
  Constrained Baseline Level<=3.1. Publisher chỉ trả H264, không fallback VP8.
- iceServers nhận định dạng RTCIceServer của aiortc: urls, username, credential
  khi cần TURN. Không log credentials hoặc đưa thông tin TURN thật vào ví dụ bàn giao.
- Client hiện dùng SDP chứa candidates sau gathering. Không xử lý message candidate
  trickle riêng; cần thống nhất với backend nếu thay cơ chế ICE.

Pi trả đúng schema:

```json
{"type":"answer","peerId":"11111111-1111-4111-8111-111111111111","sdp":"<ANSWER_SDP>"}
```

Nếu không có camera hợp lệ/không ready, quá giới hạn track hoặc negotiate lỗi:

```json
{"type":"camera-error","peerId":"11111111-1111-4111-8111-111111111111"}
```

Không thêm reason/cameraId vào message này khi dùng backend strict hiện tại.
Thông tin lỗi chi tiết cần ghi log local có kiểm soát, không xuất khóa hoặc SDP.

Server thu hồi/đóng phiên bằng:

```json
{"type":"close-peer","peerId":"11111111-1111-4111-8111-111111111111"}
```

Mỗi camera/peer có PeerConnection và SRTP session riêng. Mỗi camera vật lý dùng
một hardware encoder chung; người xem nhận cùng bitstream/chất lượng. max_peer_tracks4
là tổng số video tracks đồng thời, ví dụ2người cùng xem2camera, không phải4người
mỗi người xem2camera. Quyền backend có thể giới hạn ít hơn.

## Media, mạng và phục hồi

- Media là H264/RTP/DTLS-SRTP trực tiếp Pi→browser; WebSocket không chở video.
- Không có HTTP /offer, /video_feed, API nhận JPEG hoặc server web được mở trên Pi.
- ZeroTier là lựa chọn mạng riêng. Nếu dùng IP ZeroTier, các máy phải có đường
  mạng/quyền overlay phù hợp. SSH là kênh vận hành, không điều khiển route media.
- Mất signaling đóng các peer và đăng ký lại. Browser cần tạo phiên/offer mới theo
  lifecycle web; không cam kết giữ nguyên hình qua mọi lần đổi mạng/reboot.
- Capture lỗi đóng track cũ, dọn FFmpeg và retry. Caller có thể cần mở lại camera.
- Chưa nối PLI tới lệnh ép IDR tức thì; chờ IDR định kỳ. Queue tối đa2access units,
  overflow bỏ chuỗi phụ thuộc và chờ IDR. Chưa tự chỉnh bitrate FFmpeg theo REMB.

## Nghiệm thu chung

1. doctor kiểm phần cứng, quyền thiết bị, nguồn; benchmark file và decode output.
2. Publish đúng key/kit, quan sát published. Thử key sai trong môi trường test riêng,
   không lấy kit/publisher live để kiểm thử thu hồi hoặc vượt quyền.
3. Sinh viên đúng quyền mở từng camera và cả hai. Kiểm codec H264, decoded frames
   tăng, FPS theo cửa sổ thời gian, loss/jitter/RTT và delay đo bằng đồng hồ trong hình.
4. Test đóng/thu hồi phiên, reconnect, người vào muộn, mạng chậm; đếm peer cleanup.
5. Nếu public Internet, bên hạ tầng kiểm WSS/HTTPS, ICE/TURN, firewall và lifecycle.
   Demo trên ZeroTier chưa chứng minh mọi mạng công cộng xem được.

Kết quả đã biết và giới hạn30FPS nằm trong [verification](verification.md) và
[benchmark](benchmark.md). Không dùng cảm nhận mượt hoặc CPU trung bình làm bằng
chứng toàn bộ SLA. Profile15FPS là baseline demo, không phải cam kết phần cứng khác.
