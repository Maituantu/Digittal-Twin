# Hướng dẫn kết nối USB UART từ Windows vào WSL 2

Tài liệu hướng dẫn chuyển thiết bị USB serial đang hiển thị dưới dạng cổng COM trên Windows vào WSL 2 để chạy chương trình gateway.

## 1. Cài usbipd-win trên Windows

Mở **PowerShell bằng quyền Administrator**, chạy:

```powershell
winget install --interactive --exact dorssel.usbipd-win
```

Sau khi cài, mở lại PowerShell và liệt kê thiết bị USB:

```powershell
usbipd list
```

Tìm dòng USB serial của bạn, ví dụ:

```text
BUSID  VID:PID    DEVICE                         STATE
4-4    1a86:7523  USB-SERIAL CH340 (COM5)         Not shared
```

Trong ví dụ này, BUSID là `4-4`. Các bước tiếp theo phải thay `4-4` bằng BUSID thực tế của thiết bị.

## 2. Chia sẻ và gắn thiết bị vào WSL

Mở Ubuntu WSL và giữ cửa sổ đó chạy. Đóng các phần mềm Windows đang mở cổng COM, ví dụ Serial Monitor hoặc terminal serial.

Trong **PowerShell Administrator**, chạy:

```powershell
usbipd bind --busid 4-4
usbipd attach --wsl --busid 4-4
```

`bind` chia sẻ thiết bị; `attach` gắn thiết bị đã chia sẻ vào WSL.

## 3. Kiểm tra cổng serial trong WSL

Trong **terminal Ubuntu WSL**, chạy:

```bash
ls -l /dev/ttyUSB* /dev/ttyACM*
```

Thiết bị thường xuất hiện dưới dạng `/dev/ttyUSB0` hoặc `/dev/ttyACM0`. Dùng đúng tên cổng xuất hiện trên máy của bạn. Nếu một trong hai nhóm tên không tồn tại, `ls` có thể báo lỗi cho nhóm đó; kiểm tra các cổng còn lại trong kết quả.

Nếu tài khoản chưa có quyền truy cập cổng serial, chạy:

```bash
sudo usermod -aG dialout "$USER"
```

Sau đó thoát và mở lại phiên đăng nhập WSL để nhận quyền mới.

## 4. Cấu hình và chạy gateway

Mở file `src/system/sys_config.h` trong thư mục dự án `1.code_pi`, sửa theo cổng vừa tìm được:

```cpp
#define UART_ENABLED 1
#define UART_DEVICE "/dev/ttyUSB0"
#define UART_BAUD 115200
```

Nếu thiết bị là `/dev/ttyACM0`, thay giá trị `UART_DEVICE` tương ứng. Baud phải khớp với cấu hình UART của MCU.

Trong `src/main.cpp`, đặt cả hai cờ test về `0` để chạy hệ thống:

```cpp
#define MAIN_USE_MQTT_TEST 0
#define MAIN_USE_UART_TEST 0
```

Trong **terminal WSL**, chuyển đến thư mục `1.code_pi`, build và chạy:

```bash
cmake --build build --parallel
./build/gateway
```

Lệnh build trên dùng thư mục `build` đã được cấu hình bằng CMake trong WSL. Nếu chưa cấu hình, chạy trước:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```

Nhấn `Ctrl+C` để dừng gateway trước khi tháo thiết bị.

## 5. Kết nối lại hoặc trả thiết bị về Windows

Sau khi rút/cắm lại USB hoặc khởi động lại WSL, có thể cần gắn lại thiết bị. Trong PowerShell, kiểm tra BUSID rồi chạy:

```powershell
usbipd list
usbipd attach --wsl --busid 4-4
```

Khi muốn trả thiết bị về Windows, chạy trong PowerShell:

```powershell
usbipd detach --busid 4-4
```

Trong lúc thiết bị đang được attach vào WSL, Windows không sử dụng được cổng COM đó.

## Tài liệu tham khảo

[Microsoft Learn — Connect USB devices to WSL](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)
