# Cài đặt, chạy và bàn giao vận hành

Bản0.1.0-demo: camera publisher độc lập. Không có backend/frontend web hoặc MQTT
trong gói; ghép theo [integration-contract.md](integration-contract.md).

## Phần cứng và mạng cần có

- Pi3B là phần cứng đã thử; Linux64-bit aarch64, Python3.11+.
- USB camera có MJPG ở kích thước/FPS yêu cầu, ví dụ hai UGREEN640x480.
- FFmpeg có h264_v4l2m2m, driver V4L2 encode thực sự mở được; không tự fallback CPU.
- Nguồn/cáp ổn, đủ điện cho camera. Pi mới không mặc nhiên tương đương Pi3B; Pi5
  chẳng hạn cần đánh giá encoder khác, không lấy Docker làm cách thay driver.
- Internet/đường LAN phù hợp và ZeroTier nếu dùng endpoint mạng riêng hiện tại.
- Backend cấp URL, kit riêng và publish key; sinh viên có quyền/lượt hợp lệ để xem.

## Nhận và kiểm tra gói

Có thể dùng tar.gz trên Pi hoặc zip trên PC. Hai file chứa cùng source.
Checksum kiểm file nhận có đúng bản đóng gói, không thay thế việc xác thực người gửi.

```bash
# Trong thư mục chứa hai archive và file .SHA256SUMS:
sha256sum -c digital_twin_cam-0.1.0-demo.SHA256SUMS

# Trên Pi, giải nén dưới user thường:
tar -xzf digital_twin_cam-0.1.0-demo.tar.gz
cd digital_twin_cam-0.1.0-demo
sha256sum -c MANIFEST.sha256
```

Nếu chỉ nhận tar.gz, kiểm dòng checksum dành cho nó; SHA256SUMS liệt kê cả zip và
tar.gz nên công cụ có thể báo thiếu file zip. Chỉ giải nén vào thư mục mới, không
chép đè môi trường đang chạy khi chưa lên lịch chuyển phiên. Không dùng sudo tar.

RELEASE.json ghi nhãn bản bàn giao và MANIFEST.sha256 ghi hash từng file source.
Venv, log, video, khóa thật, config local và .git không nằm trong gói.

## Cài môi trường — trên Pi nhận gói

```bash
python3 --version
uname -m
sudo apt update
sudo apt install -y python3-venv ffmpeg v4l-utils
bash scripts/setup.sh
.venv/bin/python -m pip check
```

setup.sh tạo .venv mới ngay trên Pi, cài phiên bản trong requirements.lock.txt và
package editable. Không chép .venv x86 từ PC sang ARM Pi. Pip cần Internet khi
cài; đây chưa phải gói offline chứa sẵn wheel cho mọi nền tảng.

Nếu venv thiếu ensurepip, cài gói venv khớp Python mặc định của OS rồi chạy setup
lại. Không dùng sudo pip hoặc cài aiortc toàn hệ thống. Không cần compile Python
lại khi chỉ đổi YAML; sau thay source/cấu hình phải restart chương trình/service.

## Cấu hình và khóa

```bash
# Tạo đúng một lần cho máy mới, không ghi đè local.yaml bản đang vận hành.
cp config/pi-15fps.example.yaml config/local.yaml
nano config/local.yaml
```

| Trường | Bên nhận cần điền |
| --- | --- |
| signaling.url | Endpoint WS/WSS đúng hệ thống, ví dụ /signaling; không phải URL SSH |
| signaling.kit_code | Mã kit riêng do backend cấp; không cho hai Pi cùng chiếm kit123 |
| signaling.publish_key_file | File khóa ngoài source, đúng tài khoản service |
| cameras[].device | Thiết bị capture video, ưu tiên /dev/v4l/by-path/...-video-index0 |
| cameras[].index | Camera logic0 và1 trên giao diện |
| cameras[].enabled | Chỉ bật camera có thật, false nếu không dùng |
| fps / gop / bitrate | Baseline15 /15 /1000000; profile30FPS chỉ thử sau khi đo |

Trên Pi tạo thư mục nhận khóa; người vận hành chuyển riêng khóa chính thức vào đó:

```bash
install -d -m 700 ~/.local/share/digital_twin_cam
# Ví dụ cài file đã nhận từ kênh riêng, thay đường dẫn nguồn thật:
install -m 600 /DUONG_DAN_KHOA_DUOC_CAP \
  ~/.local/share/digital_twin_cam/publish.key
```

Không tạo khóa tùy ý rồi mong backend chấp nhận. Không gửi mật khẩu/khóa cùng gói
source. Nếu file đã có ở đúng vị trí, chỉ kiểm quyền, không cần cấp/chép lại mỗi boot.

```bash
v4l2-ctl --list-devices
id
.venv/bin/dt-cam --config config/local.yaml check
.venv/bin/dt-cam --config config/local.yaml doctor
```

User chạy tay cần quyền thiết bị video. Nếu thiếu nhóm video, thêm bằng
sudo usermod -aG video "$USER" rồi đăng nhập lại. Không chạy publisher bằng root.
Nguồn có active flags hoặc USB lỗi phải được ghi trong báo cáo, không bỏ qua chỉ
vì đã thấy hình. doctor trả không đạt khi đang có power/throttle flags là có chủ ý.

## Chạy thử thủ công

Trước khi chạy: ngừng publisher cũ cùng kit và chương trình đang chiếm camera.
Không tự tắt gateway/MQTT; đó là phần khác của hệ thống. Trên Pi có thể xem tiến
trình giữ thiết bị bằng sudo fuser -v /dev/video0 /dev/video2.

```bash
.venv/bin/dt-cam --config config/local.yaml publish
```

Chờ published/registered, camera ready rồi sinh viên mở phiên hợp lệ trên web.
Mở cả hai camera, kiểm H264 và decoded frames. Ctrl+C dừng thủ công. Mất SSH có
thể dừng publisher chạy trong terminal; dùng systemd cho vận hành sau thử nghiệm.

Muốn kiểm encoder trước khi ghép web, dừng publisher trước:

```bash
mkdir -p artifacts
.venv/bin/dt-cam --config config/local.yaml benchmark \
  --camera-index 0 --seconds 20 --output artifacts/camera1-first.h264
ffprobe -v error -select_streams v:0 -count_frames \
  -show_entries stream=codec_name,profile,width,height,level,nb_read_frames \
  -of json artifacts/camera1-first.h264
ffmpeg -v error -i artifacts/camera1-first.h264 -f null -
```

Không chạy benchmark mở lại cùng camera khi publisher đang dùng nó. File output
phải là file mới. Không suy FPS chỉ từ r_frame_rate trong raw H264; tính frame
counter theo thời gian thực và đo thêm browser.

## Bật tự chạy bằng systemd

Chạy các lệnh dưới user thường đang sở hữu project/khóa, không ở shell root.
Đường dẫn cài và user có thể khác Pi của người gửi; renderer sẽ lấy đúng chúng.
Không cần Docker. Unit yêu cầu nhóm video và network/ZeroTier tương ứng hệ thống.

Đầu tiên dừng publisher thủ công bằng Ctrl+C. Nếu chuyển khỏi dịch vụ cũ đang
cài trên Pi, chủ động chạy sudo systemctl disable --now digital-twin-camera;
Pi mới không có dịch vụ đó thì bỏ qua. Không disable digital-twin-gateway.

```bash
.venv/bin/dt-cam --config config/local.yaml check
mkdir -p artifacts

python3 scripts/render-service.py --user "$USER" --config config/local.yaml \
  > artifacts/digital-twin-cam.service

# Đọc unit và kiểm tra trước khi cài; renderer chưa thay đổi systemd.
cat artifacts/digital-twin-cam.service
systemd-analyze verify artifacts/digital-twin-cam.service

sudo install -m 644 artifacts/digital-twin-cam.service \
  /etc/systemd/system/digital-twin-cam.service
sudo systemctl daemon-reload
sudo systemctl enable --now digital-twin-cam

systemctl status digital-twin-cam --no-pager
journalctl -u digital-twin-cam -n 60 --no-pager
```

Chạy setup.sh hoặc renderer không bật service. Chỉ enable --now mới bật tự chạy
và start hiện tại. Unit chạy user thường, thêm nhóm video, dọn process group,
restart khi lỗi process. Mất signaling được xử lý bằng retry bên trong chương trình;
active không đồng nghĩa đã có hình. Camera vẫn encode khi đang chờ người xem.

Các lệnh vận hành:

```bash
# Xem log liên tục, Ctrl+C chỉ dừng xem log, không dừng camera:
journalctl -u digital-twin-cam -f

# Sau khi sửa local.yaml/source:
sudo systemctl restart digital-twin-cam

# Dừng hiện tại:
sudo systemctl stop digital-twin-cam

# Dừng và bỏ tự chạy sau boot:
sudo systemctl disable --now digital-twin-cam
```

Khi systemd đang chạy, không chạy thêm publish thủ công. Khóa chỉ đọc từ file,
không truyền trên ExecStart. Giữ quyền0600, cùng owner với user trong unit. Đổi
user/thư mục cần render/install lại unit; đổi YAML chỉ restart service.

## Thực hành reboot và đổi Wi-Fi

Theo [setup-from-scratch.md](setup-from-scratch.md), lưu Wi-Fi hệ thống, bật
NetworkManager và zerotier-one. Máy mới join network PRIVATE phải được quản trị
viên cấp quyền, không chỉ sao chép IP của Pi cũ. Không chép identity ZeroTier
hoặc SSH host keys giữa hai máy.

Chỉ reboot khi đã sẵn sàng ngắt phiên hiện tại, sau khi service đã start thành công:

```bash
sudo reboot
```

Sau boot: SSH qua IP hiện tại hoặc ZeroTier rồi kiểm tra:

```bash
nmcli device status
ip -4 route show default
sudo zerotier-cli listnetworks
systemctl is-active digital-twin-cam
journalctl -u digital-twin-cam -b -n 60 --no-pager
```

Không cần setup/scp/join/lấy khóa lại sau mỗi boot. DHCP có thể đổi IP Wi-Fi.
Mở lại phiên camera trên web sau reboot. NetworkManager/ZeroTier enabled chưa
là bằng chứng mạng tới server; kiểm tra health theo URL đúng hệ thống.

## Bảng lỗi ngắn

| Hiện tượng | Kiểm tra |
| --- | --- |
| File config không thấy | cd đúng thư mục; --config trỏ đúng local.yaml trên Pi |
| Chỉ một hình | enabled, device, index của cả hai; restart publisher sau sửa YAML |
| Encoder init lỗi | FFmpeg có wrapper chưa đủ; kiểm driver bcm2835-codec/encode, quyền và log |
| Signaling timeout | Internet, ZeroTier, route, cổng/WS endpoint; không tự kết luận khóa sai |
| registered, chưa có offer | Quyền sinh viên, kit/MQTT/attempt và thao tác mở camera |
| connected nhưng đen | ICE media, H264 profile/SPS/PPS/IDR, decoded frames, firewall |
| Buffer corrupt /0x50005 | Nguồn/cáp/USB và tải, thử một camera; không quy tất cả cho codec |
| Chạy tay được, service lỗi | User/path/key owner, nhóm video, process cũ giữ camera, journal |

## Giới hạn và kết quả nghiệm thu

User đã xem được hai camera trên web chính thức ở15FPS và báo mượt; phần cứng
H264 và file301frame đã được giải mã kiểm tra. Test PC kiểm bitstream, hai peer
và lifecycle. Chưa có số đo SLA dài hạn, và30FPS đã từng giật, buffer corrupt,
undervoltage/throttling. Bản này là demo tích hợp, không cam kết30FPS mọi Pi.

Shared encoder hiện chưa tự thích ứng bitrate riêng cho từng người xem, chưa ép
IDR tức thì theo PLI. Cần benchmark hardware/nguồn/mạng/browser của bên nhận;
không dùng chỉ CPU tổng hoặc cảm nhận mượt để nghiệm thu.

## Đóng gói lại và kiểm thử source

```bash
.venv/bin/python -m unittest discover -s tests -v
python3 scripts/package.py --release 0.1.0-demo --output-dir dist
```

Builder dùng danh sách source cho phép, chỉ lấy3config mẫu và docs/source/scripts,
không dùng tar toàn thư mục làm lộ local.yaml/key/.venv. Đầu ra gồm tar.gz, zip,
checksum bên ngoài; mỗi archive có RELEASE.json và MANIFEST.sha256. Chạy lại phải
chọn nhãn mới hoặc chuyển các archive cũ đi, tránh ghi đè nhầm bản đã gửi.
