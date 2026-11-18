# Test nhận UART trong WSL bằng Hercules và hai USB UART

## 1. Đấu nối và cấu hình

- TX của USB UART phía Windows → RX của USB UART phía WSL.
- RX của USB UART phía Windows → TX của USB UART phía WSL.
- GND hai USB UART nối chung. Không cần nối VCC giữa hai adapter; dùng mức tín hiệu UART tương thích.
- Chỉ attach USB UART phía WSL vào WSL. USB UART còn lại giữ ở Windows để mở bằng Hercules.

Trong Hercules, chọn tab Serial, chọn COM của adapter phía Windows và cấu hình **115200 baud, 8 data bits, parity none, 1 stop bit, không flow control**. Nhấn Open.

Trong vùng Send, bật chế độ **HEX** cho ô gửi. Dán các chuỗi HEX bên dưới rồi nhấn Send. Không gửi chúng dưới dạng chuỗi ASCII, không thêm CR/LF và không bật gửi lặp lúc kiểm tra từng bản tin.

Trong app, đặt `UART_DEVICE` trong `src/system/sys_config.h` đúng cổng WSL, ví dụ `/dev/ttyUSB0`, baud 115200. Đặt cả hai cờ test trong `src/main.cpp` về 0, build rồi chạy gateway từ thư mục `1.code_pi`:

```bash
cmake --build build --parallel
./build/gateway
```

App phải khởi tạo UART thành công. Các ca nhận bên dưới không cần gửi lệnh MQTT trước; callback hiện tại giải mã và in ACK/telemetry trực tiếp, không kiểm tra ACK có khớp một lệnh đã gửi hay không.

## 2. Định dạng bản tin

```text
AA | ID | LEN | PAYLOAD (LEN byte) | CRC_LOW | CRC_HIGH
```

CRC16 dùng giá trị ban đầu `0xFFFF`, đa thức phản chiếu `0xA001`, tính trên `ID + LEN + PAYLOAD`, không gồm byte `AA`. CRC truyền byte thấp trước.

## 3. Các ca kiểm tra

### TC01 — ACK hợp lệ

Giả lập ACK cho lệnh đặt điện áp `0x03`, result 0, error 0:

```text
AA 80 03 00 03 00 25 6A
```

Log mong đợi trong WSL:

```text
UART ACK: command=0x03 result=0 error=0
```

### TC02 — NACK hợp lệ

Giả lập phản hồi từ chối cho lệnh `0x03`, result 1, error 4:

```text
AA 80 03 01 03 04 75 69
```

Log mong đợi:

```text
UART ACK: command=0x03 result=1 error=4
```

Code dùng chung nhãn log `UART ACK` cho cả ACK và NACK; phân biệt qua `result`. Ca này chỉ kiểm tra giải mã số error 4.

### TC03 — Telemetry hợp lệ, payload 20 byte

Giá trị: điện áp 9 V, dòng điện 125 mA, từ trường 1.5 mT, power 1, scale 2, thước 120 mm.

```text
AA 81 14 00 00 10 41 00 00 FA 42 01 00 00 00 00 00 00 F8 3F 02 78 00 E0 80
```

Log mong đợi:

```text
UART telemetry: voltage=9.000 current_ma=125.000 field_mt=1.500000 power=1 scale=2 ruler=120
```

App xếp ACK vào hàng chờ và gửi ngược về Hercules. Bật hiển thị HEX ở vùng nhận Hercules để kiểm tra:

```text
AA 80 03 00 81 00 45 CA
```

Payload `00 81 00` nghĩa là result 0, xác nhận bản tin telemetry `0x81`, error 0. Log app có thêm `UART telemetry ACK queued for TX`; đây là thông báo đã xếp vào bộ đệm gửi, chưa phải xác nhận Hercules đã nhận. Nếu UART đang bận, app giữ ACK để thử lại. Hàng chờ ACK có tối đa `COMMAND_QUEUE_CAPACITY` phần tử; khi đầy, app báo `UART telemetry ACK queue full`.

### TC04 — Telemetry có padding, payload 24 byte

Cùng giá trị với TC03, dùng layout 24 byte mà decoder đang hỗ trợ:

```text
AA 81 18 00 00 10 41 00 00 FA 42 01 00 00 00 00 00 00 00 00 00 F8 3F 02 00 78 00 E2 6A
```

Log và ACK trả về giống TC03.

### TC05 — Telemetry có padding, payload 32 byte

```text
AA 81 20 00 00 10 41 00 00 FA 42 01 00 00 00 00 00 00 00 00 00 00 00 00 00 F8 3F 02 00 78 00 00 00 00 00 A8 E7
```

Log và ACK trả về giống TC03.

### TC06 — CRC sai

ACK của TC01 nhưng cố tình đổi CRC cuối từ `6A` thành `6B`:

```text
AA 80 03 00 03 00 25 6B
```

Mong đợi: không in ACK cho bản tin này. Parser hiện tại bỏ frame sai CRC mà không in lỗi. Sau đó gửi TC01 để xác nhận app vẫn nhận được bản tin hợp lệ.

### TC07 — ACK có giá trị không hợp lệ, CRC đúng

Trường result bằng 2, trong khi decoder chỉ chấp nhận 0 hoặc 1:

```text
AA 80 03 02 03 00 84 AA
```

Log mong đợi:

```text
UART invalid ACK
```

### TC08 — Telemetry sai độ dài, CRC đúng

Payload chỉ có 1 byte, trong khi decoder hỗ trợ 20, 24 hoặc 32 byte:

```text
AA 81 01 00 20 78
```

Log mong đợi:

```text
UART invalid telemetry length/value: 1
```

### TC09 — Hai frame liền nhau

Gửi một lần chuỗi ACK + NACK:

```text
AA 80 03 00 03 00 25 6A AA 80 03 01 03 04 75 69
```

Mong đợi: hai dòng log tương ứng TC01 và TC02, đúng thứ tự.

### TC10 — Frame bị ngắt giữa chừng và hết timeout

Gửi phần đầu:

```text
AA 80 03
```

Chờ ít nhất 1 giây rồi gửi phần còn lại:

```text
00 03 00 25 6A
```

Với `UART_RX_TIMEOUT_MS = 200`, mong đợi không có log ACK vì phần đầu đã hết hạn. Sau đó gửi nguyên TC01, mong đợi lại nhận ACK bình thường.

## 4. Phạm vi kiểm tra và xử lý khi không thấy log

Các ca trên kiểm tra chiều **Hercules → UART → parser → callback của app** và chiều ACK từ app về Hercules khi nhận telemetry hợp lệ. Frame sai CRC hoặc telemetry không hợp lệ không được ACK. App không phản hồi cho bản tin ACK/NACK để tránh lặp phản hồi, và chưa publish telemetry lên MQTT.

Nếu không thấy log ở TC01, kiểm tra đúng COM phía Windows, đúng thiết bị `/dev/ttyUSB*` hoặc `/dev/ttyACM*` phía WSL, dây TX/RX nối chéo, GND chung, baud khớp và chế độ gửi HEX. Không mở cùng cổng WSL bằng một chương trình serial khác trong khi gateway đang chạy.

Các chuỗi HEX được tính theo codec trong repository; cần chạy thực tế để xác nhận toàn bộ đường truyền và hai adapter.
