# UART Pi ? ESP32 / STM32

## Reference implementation

Analyzed `3. VDK/PC1/lib/msg/bts_frame_message.c`, `bts_get_message.c`,
`lib/newsletter/data_message.*`, `Handler_Data.c`, `get_message.cpp`,
`send_message.cpp` and `src/main.cpp`. The supplied project runs Arduino/FreeRTOS
on ESP32; compatibility with a separate STM32 build needs verification on hardware.

Frame: `AA | command ID (1) | payload length (1) | payload | CRC low | CRC high`.
CRC16: initial FFFF, reflected polynomial A001, covers ID + length + payload,
excluding AA. Maximum payload is 140 bytes, matching CreateMessage.
Serial: 115200 baud, 8N1, no flow control.

Commands 01..06 match kit_messages.h. Reset, power, voltage, scale, shutdown
have one-byte payload; ruler is uint16 little-endian. Voltage accepts only
0, 3, 6, 9, 12, matching Is_Valid_Voltage on the MCU.
ACK 80 has three bytes: ack_code, command_id, error_code.

## Files and application flow

- `src/uart/bts/`: framing, CRC and byte conversion copied from the VDK BTS library.
  CreateMessage builds frames; DetectMessage checks complete received frames.
  Local header adaptations expose CheckSum and C linkage for C++ callers; algorithms are unchanged.
- `src/uart/uart_messages.*`: command/data adapters, bounded stream assembly, ACK/telemetry decoding using BTS framing.
- `src/uart/uart_client.*`: Linux port configuration, nonblocking partial writes/reads,
  RX inter-byte timeout and TX timeout. Main-thread ownership, no worker thread.
- `sys_manager`: initializes UART before MQTT; retains a command while TX is busy;
  polls RX/TX every iteration and logs ACK/telemetry.

Success from send means queued locally, not MCU execution. No automatic retries,
ACK timeout, or one-command-at-a-time ACK orchestration is implemented. This avoids
blindly repeating commands after ambiguous delivery. Shutdown command resets MCU
state in the reference firmware; it does not power off Linux.

Telemetry is decoded/logged, not automatically published to MQTT in this change.

## Configuration and running

Edit `src/system/sys_config.h`:

```cpp
#define UART_ENABLED 1
#define UART_DEVICE "/dev/serial0"
#define UART_BAUD 115200
#define UART_RX_TIMEOUT_MS 200
#define UART_TX_TIMEOUT_MS 1000
```

Use `/dev/ttyUSB0`, `/dev/ttyACM0`, or a stable `/dev/serial/by-id/...` for USB.
UART_ENABLED=0 allows MQTT-only operation, logging that dequeued commands are dropped.
With UART enabled, failure to open/configure the device stops startup. A runtime
I/O error stops the gateway; restart after correcting the device connection.
Linux user needs access to the serial device. GPIO UART needs serial hardware
enabled and the Linux serial console disabled. Use 3.3V UART levels, cross TX/RX,
and share GND; do not connect RS232 voltage levels directly.

Build on Linux/Pi/WSL:

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/gateway
```

The codec test checks CRC, command serialization, corrupt frame recovery, ACK,
and three telemetry layouts. The PTY test checks actual termios TX and fragmented
RX without a physical MCU. Neither substitutes for a Pi?MCU hardware test.

## Reference firmware issues to resolve

1. `create_frame_telemetry` sends `sizeof(telemetry_data)` directly. Padding and
   double alignment depend on the MCU ABI. Pi accepts explicit little-endian
   IEEE754 layouts of 20 bytes (packed), 24 bytes (double offset 12), and 32 bytes
   (double offset 16). Confirm received length/values on the target; prefer explicit
   20-byte serialization on the MCU for a stable protocol. DATA_MESSAGE_MAX_PAYLOAD
   says 20, but CreateMessage actually permits 140; do not assume sizeof is 20.
2. Current setup enables only Task_Get_Message. Each received frame is put into
   a 10-item FreeRTOS queue with portMAX_DELAY and no active consumer. The receive
   task can block on the 11th frame. Remove that unused queue send or redesign
   task ownership. Simply enabling Task_Send_Message would process commands twice
   because the receive task already calls HandleDataMessage and writes ACK.
3. The supplied active tasks do not call create_frame_telemetry periodically.
   Pi cannot receive measurements until firmware sends them.
4. DebugMessage/debug_print_system_data use the same serial console as binary
   traffic. Separate debug logs from protocol UART. Pi skips noise and validates
   CRC, but debug output should not share the production protocol stream.
5. MCU receive buffer is 128 while framing permits 145 bytes. Most command cases
   also do not validate payload length. Harden the MCU parser before accepting
   untrusted/noisy serial input; the Pi parser validates bounds and CRC.

The reference firmware was read only and has not been changed.

The MCU-specific bts_get_message parser is not used on Pi: its global flag,
call-count timeout and unchecked output buffer are unsuitable for Linux fragmented
reads. The Pi accumulates bounded input and calls BTS DetectMessage only after a
complete frame is present. Arduino/FreeRTOS tasks and MCU command handlers remain
on the MCU; they are not Linux UART drivers.
