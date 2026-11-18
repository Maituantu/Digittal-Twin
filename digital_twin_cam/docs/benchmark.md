# Kế hoạch benchmark và khoanh vùng lỗi

Không dùng “H264” hoặc `h264_v4l2m2m có tên trong list` làm kết luận nhẹ/mượt.
Nghiệm thu theo các chặng dưới đây. Mỗi lần chỉ đổi một biến và giữ bằng chứng.

## Chặng A — phần cứng không lấy camera

Video synthetic ngắn kiểm tra driver V4L2 encode, profile/level và giải mã đầu ra.
Không stop dịch vụ camera cũ; không dùng FPS của synthetic chạy nhanh hơn realtime
để tuyên bố camera đạt tốc độ. Kết quả hiện có trong verification.

## Chặng B — camera thật, chưa dùng mạng

- `doctor`, nhiệt độ, throttling, trạng thái USB trước thử.
- Stop chương trình đang chiếm camera sau khi đã dừng phiên sử dụng của nó.
- `benchmark` một camera VGA15 khoảng20–60giây như README, kiểm tra ffprobe/decode.
- Giữ log stderr: driver encode, dropped frames, ioctl, timeout và USB disconnect.
- Hết thử phải dọn child và camera; muốn dùng lại bản cũ phải stop publisher mới trước.

Đo song song **trên Pi** trong terminal khác:

```bash
vcgencmd measure_temp
vcgencmd get_throttled
ps -eo pid,comm,pcpu,pmem,args --sort=-pcpu | head -15
```

Không chia sẻ nguyên output args nếu chương trình khác có token/khóa trong command line.
Trong benchmark mới khóa chỉ đọc từ file và không truyền vào FFmpeg argv.
Có thể dùng `top -H`/`pidstat` nếu đã có sẵn; đo cả FFmpeg và Python, không chỉ một PID.
Cờ0x50005 đã từng có undervoltage và đang undervoltage/throttled; sửa nguồn/cáp,
USB hub có nguồn nếu cần trước khi kết luận CPU/codec. Không benchmark dài khi nguồn không ổn.

## Chặng C — publisher và một browser hợp lệ

Đăng ký chính thức, nhận offer rồi xem `chrome://webrtc-internals` / thống kê browser:
codec thực là H264, FPS decode, framesDropped, packetsLost, jitter, RTT, jitterBufferDelay.
So với frame counter/log Pi; đặt đồng hồ trong cảnh camera để đo trễ đầu-cuối.
Nếu Pi xuất frame đều nhưng browser chậm, kiểm tra ICE/network/decoder, không tự kết luận Pi yếu.
Kiểm tra đăng ký không có offer có thể do attempt/quyền/MQTT; không phải lỗi encoder.

## Chặng D — tải tăng và phục hồi

| Vòng | Camera | Profile | Người xem | Mục đích |
| --- | --- | --- | --- | --- |
| B1 | 1 | 640x480 / 15fps / 1Mbps | 0, ghi file | USB + JPEG decode + encoder |
| C1 | 1 | Như B1 | 1 | ICE/decoder/latency |
| C2 | 1 | Như B1 | 2, sau đó3 | Băng thông + SRTP/queue/IDR |
| D1 | 2 | Mỗi camera như B1 | 1 | USB, nguồn, hardware encoder chia sẻ |
| D2 | 1 rồi2 | 30fps, GOP30 nếu ổn | 1 rồin | Chỉ thử sau các vòng trước đạt |

Các số là điểm bắt đầu thử, không phải công suất cam kết. Số video tracks = số
camera được xem trong các phiên, không phải số cổng HTTP hay số core.
Thử người vào muộn, mạng một người chậm, Stop/Start, rút camera, WS reconnect,
thu hồi attempt, SIGTERM. Đo bao lâu có IDR lại; không có điều khiển PLI tức thì.

## Bảng ghi kết quả

Ghi ngày, OS/kernel/FFmpeg, camera by-path, dây/nguồn, lệnh, độ dài, profile/level,
frame decode, FPS cửa sổ ổn định, CPU FFmpeg/Python, RAM, nhiệt độ, throttle flags,
bitrate mỗi peer/tổng, RTT/loss/jitter, delay đầu-cuối, thời gian hồi phục và kết luận.
Tiêu chí FPS/latency thực dụng cần thống nhất theo bài thực hành; không tự gắn nhãn
production-ready chỉ vì test ngắn không báo lỗi. File thu hình ở artifacts/ không vào Git.


## So sánh đang xem web ở 15 FPS và 30 FPS

Dùng `scripts/monitor.py` trên Pi trong terminal SSH thứ hai, giữ nguyên phiên
xem. Tool chỉ đọc /proc, nhiệt độ và cờ nguồn; không mở camera mới, không restart
publisher, không in argv/token. Không chạy lệnh `benchmark` mở lại cùng camera
khi publisher còn đang dùng thiết bị.

```bash
cd ~/digital_twin_cam
mkdir -p artifacts
.venv/bin/python scripts/monitor.py --seconds 60 \
  --output "artifacts/web-15fps-$(date +%Y%m%d-%H%M%S).json"
```

Trong 60 giây, cùng một browser xem cả hai camera, chuyển động cùng một cảnh.
Ghi thêm FPS nhận/decoded frames/framesDropped/packetsLost/jitter/RTT trong
chrome://webrtc-internals. Lấy chênh lệch `frames` giữa hai log Capture metrics chia
cho chênh lệch thời gian, không dùng trung bình gồm khởi động để so sánh FPS ổn định.

Lưu bản cấu hình 15FPS. Đổi mỗi camera `fps: 30`, `gop: 30` để giữ GOP khoảng1giây;
giữ nguyên độ phân giải và bitrate1Mbps trong lần so sánh đầu. Dừng publisher
Ctrl+C, chạy lại cùng lệnh publish, mở lại hai camera trên web rồi đo:

```bash
.venv/bin/python scripts/monitor.py --seconds 60 \
  --output "artifacts/web-30fps-$(date +%Y%m%d-%H%M%S).json"
```

CPU100% của tool nghĩa là đầy một logical CPU, không phải toàn bộ Pi. Tổng RSS
có thể tính trùng shared pages; không coi đó là bộ nhớ PSS. Active flag samples >0
nghĩa là có cờ nguồn/nhiệt tại các thời điểm lấy mẫu; không thể tuyên bố benchmark
đạt ổn định. Lấy mẫu1giây không bảo đảm bắt mọi xung sụt áp ngắn, cần đối chiếu
journal kernel theo thời điểm thử. FPS30 ở cùng bitrate có thể giảm độ nét khi
chuyển động; điều chỉnh bitrate là vòng thử riêng.

Muốn Pi độc lập PC phải kiểm tra default route. `ip route get 10.147.19.155`
đi qua interface ZeroTier chưa chứng minh không dùng PC làm gateway. Nếu default
route là `via 10.42.0.1 dev eth0` thì Pi đang dùng PC chia sẻ mạng; server chỉ
signaling và media vẫn Pi→browser, không phải server relay.
