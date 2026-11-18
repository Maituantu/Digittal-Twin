# Triển khai và vận hành

## Trạng thái mặc định

Project mới nằm riêng khỏi `digital_twin`. Không cài service tự động, không stop
capture cũ, không copy khóa, không kết nối/chiếm kit khi chạy setup hoặc check.
README hướng dẫn chép source rồi cài trên Pi. `.venv` PC không dùng lại trên ARM Pi.
Dependency direct và transitive được chốt trong `requirements.lock.txt` cho lần thử;
kiểm thử lại trước khi nâng aiortc/PyAV vì đường encoded packet phụ thuộc tương thích thư viện.

## Danh sách trước triển khai

1. Xác nhận đúng máy Pi/IP và SSH host key. Cài lại OS có thể đổi host key; không bỏ qua
   kiểm tra hoặc xóa toàn bộ known_hosts. IP ví dụ Ethernet `10.42.0.55` cần xác minh.
2. USB UVC thật có MJPG đúng kích thước/FPS và user có quyền nhóm video.
3. FFmpeg/driver mở được `h264_v4l2m2m`; bitstream Constrained Baseline Level <=3.1.
   Lệnh dùng profile số `578`; bộ lọc `h264_metadata=level=3.1` đồng bộ SPS với SDP
   vì bản V4L2 wrapper đã thử không áp dụng `-level:v` vào SPS. Cấu hình chặn vượt
   giới hạn frame size, macroblock rate và bitrate Level3.1; không chỉ sửa nhãn cho video quá chuẩn.
4. Nguồn điện ổn; không bỏ qua active undervoltage/throttling khi đo tốc độ.
5. Không còn process khác chiếm USB; không có publisher khác đang đăng ký cùng kit.
6. Pi truy cập được endpoint WS; browser truy cập được server và địa chỉ media của Pi.
   Ping, TCP/HTTP, WS, ICE, decoded frames là các phép thử khác nhau.
7. Có khóa chính thức ở ngoài source, mode0600; không dùng khóa demo 8091.
8. Nhóm/lịch/attempt và MQTT/telemetry hợp lệ do hệ thống bên ngoài đảm nhiệm.

## Chạy nền sau khi nghiệm thu

Sửa `User`, `Group`, `WorkingDirectory`, `ExecStart` của template đúng đường dẫn
cài đặt. Ví dụ giữ `~/digital_twin_cam` cho user `thang`:

```bash
cd ~/digital_twin_cam
sudo install -m 644 deploy/systemd/digital-twin-cam.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl start digital-twin-cam
systemctl status digital-twin-cam --no-pager
journalctl -u digital-twin-cam -n 80 --no-pager
```

Chỉ khi muốn tự chạy sau bật nguồn:

```bash
sudo systemctl enable digital-twin-cam
```

Capture cũ `digital-twin-camera` và publisher mới không được tự bật cùng chiếm camera.
Nếu đã quyết định chuyển hẳn và không còn người xem cũ, dùng
`sudo systemctl disable --now digital-twin-camera`. Đây là bước vận hành chủ động,
không nằm trong setup. Muốn quay lại, dừng/disable `digital-twin-cam` rồi enable/start
`digital-twin-camera` nếu trước đây service đó được bật.

Đọc log bằng journalctl, không nhầm trạng thái systemd `active` với đã có video.
Service mở camera và mã hóa ngay cả khi chưa ai xem; bật autostart có thể tăng tải sau boot.
Template giới hạn quyền ghi, chạy user thường với nhóm video, dọn cả process group.
Không tự đặt CPUQuota vì sẽ làm sai phép đo; sau benchmark mới chọn resource policy.

## Đổi IP, kit, khóa

| Thay đổi | Sửa đâu | Có compile không? |
| --- | --- | --- |
| IP Pi | Lệnh SSH/SCP; đường mạng ICE từ browser tới Pi | Không, publisher không lưu IP Pi cố định |
| IP server/port/path | `signaling.url` trong config/local.yaml | Không; restart publisher sau sửa |
| Mã kit | `signaling.kit_code`, đúng kit được backend cấp | Không; kiểm tra không đụng publisher khác |
| Khóa publish | File ngoài source và `publish_key_file` | Không; reload bằng restart, không in khóa |
| Camera đổi số | `device`, ưu tiên by-path thay videoN | Không |
| FPS/bitrate/camera thứ hai | Từng trường camera; giữ giới hạn Level3.1 | Không; benchmark lại |

Source Python đổi: cài editable đã dùng `pip install -e .` nên đọc source mới khi
chạy lại. Dependency đổi: cập nhật pyproject/lock có kiểm thử, chạy setup lại.
Server URL public cần bên CNTT cấp HTTPS/WSS và ICE/TURN phù hợp, không suy ra từ
địa chỉ SSH. ZeroTier IP là mạng riêng, không phải ai trên Internet cũng truy cập được.
