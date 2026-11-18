# Thực hành từ đầu: Wi-Fi → ZeroTier → camera → tự chạy sau boot

Mục tiêu: Pi tự dùng Wi-Fi, kết nối ZeroTier và chạy publisher camera mà không
cần PC encode hay chia sẻ Internet. PC chỉ dùng chuyển source, SSH và mở browser.
Server chính thức đã triển khai web/auth/signaling; không cài lại hoặc sửa source server.
Media vẫn Pi → browser; ZeroTier không phải server relay.

## 1. Kết nối Wi-Fi trên Pi — chỉ thiết lập một lần cho mỗi mạng

Các lệnh trong mục này chạy trên Pi, qua console hoặc SSH đang có. Giữ Ethernet
lúc thiết lập nếu Wi-Fi chưa chạy. Không phải cài lại OS để thực hành lại project.

```bash
sudo nmcli radio wifi on
nmcli device wifi list ifname wlan0
sudo nmcli --ask device wifi connect "TEN_WIFI" ifname wlan0
```

Thay TEN_WIFI bằng SSID thật. Nhập mật khẩu khi được hỏi trong terminal; không đặt
mật khẩu vào source, YAML, command line hoặc tài liệu. Nếu Wi-Fi đã connected,
bỏ qua lệnh connect. Trên máy đã kiểm tra, profile đang dùng là FPT2G.

Lấy UUID của profile Wi-Fi đang hoạt động, tránh nhầm tên SSID với tên profile:

```bash
CAM_WIFI_UUID="$(nmcli -t -f UUID,DEVICE connection show --active \
  | awk -F: '$2 == "wlan0" {print $1; exit}')"
test -n "$CAM_WIFI_UUID" || { echo 'Wi-Fi chưa kết nối'; exit 1; }

sudo nmcli connection modify uuid "$CAM_WIFI_UUID" \
  connection.autoconnect yes \
  connection.autoconnect-retries 0 \
  connection.permissions ""

sudo systemctl enable --now NetworkManager
```

- autoconnect yes: tự kết nối khi thấy mạng đã lưu.
- retries0: tiếp tục thử lại khi router tạm chưa sẵn sàng.
- permissions rỗng: profile dùng ở mức hệ thống, không đòi người dùng đăng nhập GUI.
- enable: tự chạy sau boot; start/--now: chạy hiện tại. Không phải lệnh restart mạng.

Profile cần có mật khẩu được lưu cho hệ thống; trường psk-flags trên máy đã kiểm
tra là0. Nếu boot vẫn đòi mật khẩu, kết nối lại bằng sudo nmcli --ask để xử lý
profile/secret agent; không đọc mật khẩu ra log để kiểm tra.

```bash
ip -4 addr show wlan0
ping -I wlan0 -c 3 -W 2 1.1.1.1
ip -4 route show default
```

IP Wi-Fi do DHCP cấp có thể đổi. “Cấu hình cố định” ở đây là tự kết nối sau boot,
không phải gán cứng192.168.88.138. Nếu cần IP không đổi, cấu hình DHCP reservation
ở router hoặc dùng ZeroTier IP để SSH khi overlay hoạt động.

## 2. ZeroTier trên Pi — giữ lại giữa các lần boot

Trên Pi hiện tại đã có ZeroTier và đã join mạng; không cài lại hoặc leave/join mỗi lần.
Nếu là OS mới chưa có zerotier-cli, cài theo nguồn chính thức:

```bash
curl -fsSL https://install.zerotier.com/ -o /tmp/zerotier-install.sh
sudo bash /tmp/zerotier-install.sh
```

Sau đó, hoặc trên bản đã có:

```bash
sudo systemctl enable --now zerotier-one
sudo zerotier-cli info
sudo zerotier-cli join 9e1948db63aaaef1
sudo zerotier-cli listnetworks
```

Nếu CLI báo chưa kết nối được ngay sau start/restart, chờ dịch vụ khởi tạo rồi
chạy lại info/listnetworks; không lập tức cài lại. Mạng PRIVATE cần quản trị viên
cấp quyền cho node mới. Cài lại OS có thể tạo node ID mới, cần cấp quyền lại và
xác minh lại SSH host key. Reboot thông thường giữ nguyên identity và membership.

Kiểm tra theo từng tầng:

```bash
systemctl is-enabled NetworkManager zerotier-one
systemctl is-active NetworkManager zerotier-one
sudo zerotier-cli listnetworks
curl --connect-timeout 5 --max-time 8 http://10.147.19.155/stream-health
```

ready:true xác nhận endpoint health trả lời, chưa xác nhận khóa publish hoặc video.
ZeroTier OK/ONLINE không bảo đảm mọi peer đều thông. Sau đổi Ethernet→Wi-Fi, nếu
Internet có nhưng server không tới được, kiểm tra route/health trước. Restart
zerotier-one chỉ là bước xử lý kết nối khi cần, không phải thao tác bắt buộc mỗi boot.

## 3. Chép source — trên PC

```bash
cd /home/ducvu/duanma/digital_twin_cam

tar --exclude='__pycache__' --exclude='*.egg-info' \
  -czf /tmp/digital_twin_cam-src.tar.gz \
  pyproject.toml requirements.lock.txt README.md HANDOVER.md AGENTS.md .gitignore \
  src config/pi.example.yaml config/pi-15fps.example.yaml config/pi-30fps.example.yaml deploy scripts tests docs

scp /tmp/digital_twin_cam-src.tar.gz thang@192.168.88.138:~/
```

Thay IP bằng IP thực tế. Chỉ chép source, không chép .venv của PC sang ARM Pi.
Không cần SSH vào server web để chạy các lệnh PC này.

## 4. Cài project — trên Pi

```bash
mkdir -p ~/digital_twin_cam
cd ~/digital_twin_cam
tar -xzf ~/digital_twin_cam-src.tar.gz
sudo apt update
sudo apt install -y python3-venv ffmpeg v4l-utils
bash scripts/setup.sh
```

Tạo config một lần, giữ config đang dùng nếu đã có:

```bash
test -f config/local.yaml || cp config/pi.example.yaml config/local.yaml
nano config/local.yaml
.venv/bin/dt-cam --config config/local.yaml check
.venv/bin/dt-cam --config config/local.yaml doctor
```

Bắt đầu15FPS/GOP15. Chỉ đổi30FPS/GOP30 sau thử; config mẫu30FPS ở
config/pi-30fps.example.yaml. Mẫu không tự thay config/local.yaml. Camera thứ
hai cần đúng device và enabled:true. Kiểm tra v4l2-ctl --list-devices; ưu tiên
by-path-video-index0 để thiết bị không đổi số theo thứ tự cắm USB.

setup.sh chỉ cài môi trường; không tự đăng ký publisher, đổi boot hay dừng camera cũ.
Reboot không xóa .venv, config hoặc khóa. Chỉ cài lại dependencies khi đổi chúng
hoặc tạo máy/môi trường mới.

## 5. Khóa publish — phân biệt PC và Pi

Trên Pi:

```bash
install -d -m 700 ~/.local/share/digital_twin_cam
```

Trên PC, nếu đã có khóa chính thức ngoài repo:

```bash
scp ~/.local/share/digital_twin/official-web-publisher.key \
  thang@192.168.88.138:~/.local/share/digital_twin_cam/publish.key
```

Trên Pi:

```bash
chmod 600 ~/.local/share/digital_twin_cam/publish.key
```

Khóa này do hệ thống web cấp, không phải mật khẩu SSH, không phải viewer.key demo.
Không cần tạo/chép lại khóa sau mỗi reboot. Nếu chưa có, liên hệ người vận hành web
để cấp khóa đúng kit thay vì tạo ngẫu nhiên. publish_key_file trong local.yaml phải
trỏ tới file trên Pi, không phải đường dẫn /home/ducvu của PC.

## 6. Chạy thử thủ công — trên Pi

Dừng bằng Ctrl+C các publisher thủ công cũ của cùng kit trước khi chạy. Nếu camera
cũ còn chiếm thiết bị và đã ngừng sử dụng phiên cũ, dừng nó:

```bash
sudo systemctl stop digital-twin-camera
cd ~/digital_twin_cam
.venv/bin/dt-cam --config config/local.yaml publish
```

Giữ terminal mở. Chờ Publisher registered và camera ready. Sinh viên mở web
http://10.147.19.155/ trong lượt hợp lệ để tạo offer. Có hình và decoded frames
mới chứng minh đường video, không phải chỉ published. Kit MQTT/attempt vẫn do hệ
thống thiết bị ngoài project đảm nhiệm, không bị bỏ qua hoặc giả lập.

Để thử độc lập PC, SSH bằng Wi-Fi, dừng publisher thủ công trước khi rút Ethernet,
kiểm tra Internet rồi chạy lại publisher trong SSH Wi-Fi và mở lại camera. Wi-Fi
có thể có hiệu năng khác Ethernet; nguồn hiện vẫn từng báo0x50005, phải ghi trong benchmark.

## 7. Tùy chọn: bật camera tự chạy bằng systemd

Chỉ làm sau khi chọn được cấu hình ổn định. User thang và đường dẫn dưới đây cần
đúng máy đang cài. Template đã được chuẩn bị trong deploy/systemd.

Dừng publisher thủ công bằng Ctrl+C trước. Sau đó **trên Pi**:

```bash
cd ~/digital_twin_cam

# Chuyển hẳn khỏi capture cũ để tránh hai service cùng mở camera sau boot.
sudo systemctl disable --now digital-twin-camera

sudo install -m 644 deploy/systemd/digital-twin-cam.service \
  /etc/systemd/system/digital-twin-cam.service
sudo systemctl daemon-reload
sudo systemctl enable --now digital-twin-cam

systemctl status digital-twin-cam --no-pager
journalctl -u digital-twin-cam -n 60 --no-pager
```

Không disable digital-twin-gateway hoặc dịch vụ MQTT vì project camera không thay
thế chức năng của chúng. Unit chạy user thường, nhóm video, đợi network-online
và ZeroTier service khởi động; publisher vẫn retry vì thứ tự service không bảo đảm
Internet/ZeroTier đã thực sự thông. Unit enable không thay thế quyền truy cập web.
Camera encode ngay cả khi chưa ai xem; chọn autostart đồng nghĩa có tải sau boot.

Khi service đang chạy, đừng chạy thêm dt-cam publish thủ công. Muốn sửa YAML:

```bash
nano ~/digital_twin_cam/config/local.yaml
sudo systemctl restart digital-twin-cam
```

## 8. Thực hành reboot — chỉ chạy khi đã sẵn sàng ngắt phiên hiện tại

```bash
sudo reboot
```

Sau khi máy lên, SSH lại bằng IP hiện tại của Wi-Fi hoặc ZeroTier. Reboot làm ngắt
SSH và video hiện tại; cần mở lại camera trên web. Không cần chạy setup/scp/join lại.

```bash
nmcli device status
ip -4 route show default
sudo zerotier-cli listnetworks
systemctl is-active NetworkManager zerotier-one digital-twin-cam
journalctl -u digital-twin-cam -b -n 60 --no-pager
curl --connect-timeout 5 --max-time 8 http://10.147.19.155/stream-health
```

Chỉ mong digital-twin-cam active nếu đã cài bước7. Nếu chỉ cấu hình Wi-Fi/ZeroTier,
camera vẫn chạy bằng lệnh publish thủ công như bước6. Không suy rằng enable đã là
bằng chứng reboot thành công: phải thực hành lại sau boot và kiểm tra video.

## Tham khảo chính thức

- [NetworkManager: autoconnect, retries và permissions](https://www.networkmanager.dev/docs/api/latest/settings-connection.html)
- [ZeroTier: cài đặt Linux](https://www.zerotier.com/download/)
- [ZeroTier: join mạng và cấp quyền](https://docs.zerotier.com/quickstart/)
