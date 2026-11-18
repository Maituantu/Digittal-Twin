# Pi MQTT gateway

C++17 using C-style functions/structs, no application classes. The client uses
libmosquitto, Jansson and pthreads on Ubuntu/Raspberry Pi Linux. The JSON codec
implements the supplied UART/MQTT v1.0 document. The system forwards MQTT
commands to UART and decodes received ACK/NACK and telemetry. TLS is not implemented.

## Build and run

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libmosquitto-dev libjansson-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/gateway
```

Run without command-line arguments. By default, both test flags in `src/main.cpp`
are `0`, so the gateway runs `sys_manager_config()` and `sys_manager_run()`.
Set the UART device/baud and MQTT connection/topics in `src/system/sys_config.h`
before running on the Pi (default UART: `/dev/serial0`, 115200 baud, 8N1).
The system polls UART every loop, retains a pending command while TX is busy,
and prints decoded ACK/NACK and publishes telemetry and 1 Hz in-use heartbeats
using the server-provided `attempt_id`.
Ctrl+C/SIGTERM closes UART and stops MQTT.

For MQTT manual tests, set `MAIN_USE_MQTT_TEST=1` and `MAIN_USE_UART_TEST=0`
in `src/main.cpp`. Select the test mode in `src/test/mqtt_test.h`.
Edit `src/test/mqtt_test_client.cpp` to select:

- `MQTT_TEST_HOST`: one of the four predefined hostnames.
- `s_test_port`, client ID and optional credentials.
- `s_sub_topics[]`: any explicit filters; MQTT `+`/`#` wildcards are supported.
- `s_pub_topic` and `s_pub_payload`: the test publication destination and bytes.

The example publishes one status message to `test/pi-kit01/status` each time
subscriptions become ready, including reconnect. This is a test topic/payload,
not an approved telemetry schema. Confirm host, port and permissions with the
provider; the four hostnames are not verified connection configurations.
Do not commit real credentials. Each Pi needs its own client ID.

Build again on the Pi in a separate directory, for example `build-pi`; PC
x86-64 executables cannot run directly on ARM.

## Responsibilities

- `mqtt_client.cpp/.h`: generic transport, caller-provided SUB/PUB topics,
  connection, reconnect, SUBACK tracking and raw receive callback. It neither
  constructs kit topics nor parses JSON or filters retained messages.
- `mqtt_test.cpp/.h`: common config/run dispatcher and one mode-selection define.
  Set `MAIN_USE_MQTT_TEST` in `src/main.cpp` to `1` for tests or `0` for the system.
  In test mode, main calls `mqtt_test_config()` once, then `mqtt_test_run()` inside `while (1)`.
- `mqtt_test_client.cpp/.h`: broker settings, topics, raw SUB/PUB and retained policy.
- `mqtt_test_messages.cpp/.h`: six local decode examples, one local telemetry
  encode and live codec helpers for combined testing. Ctrl+C/SIGTERM stops/exits.
- `mqtt_messages.cpp/.h`: strict Web command decoding and telemetry JSON encoding.
  See [MQTT_MESSAGES.md](MQTT_MESSAGES.md) for API examples and draft-schema notes.
- `msg/kit_messages.h`: shared command/telemetry/ACK types from the UART design.
  This header does not implement UART stream parsing, CRC or hardware access.

`kit_code` remains an optional metadata field for configuration compatibility.
The transport never uses it to build, validate or restrict topic names.
Topic strings in `s_sub_topics[]` are explicit; changing kit metadata does not
rewrite them. Topic selection and construction belong to the upper layer.

## Calling the transport

```cpp
mqtt_config_struct_t config = {};
config.host = "127.0.0.1";
config.port = 1883;
config.client_id = "example-pi";

mqtt_client_init(&config, on_message, NULL);
mqtt_client_subscribe("my/device/command/#", 1);
mqtt_client_start();

// After mqtt_client_is_connected() is true:
const char payload[] = "hello";
mqtt_client_publish("my/device/status", payload, sizeof(payload) - 1, 1, false);
```

Check every status in real code. `subscribe()` can be called after init, before
or after start. The desired filter is copied; registration success is not a
SUBACK. Repeated filters update QoS without adding duplicate entries. Init
resets the list; stop/start and reconnect preserve/restore it. There are no
implicit subscriptions. Removing subscriptions is not implemented yet.

The application registers a callback with this signature:

```cpp
void on_message(const char *topic, const void *payload, size_t length,
                bool retained, void *context);
```

Topic is the actual received topic; payload may be binary, empty or retained.
Both are borrowed for the callback duration. Copy to a synchronized queue if
processing later in another thread. The callback runs on the network thread:
do not block, call init/start/stop, or invoke the network loop inside it.
JSON validation, topic routing, duplicate-command handling and retained policy
belong to the upper layer before forwarding commands to STM32. In combined
mode the manual test calls mqtt_messages_decode_command(), prints the decoded
ID/value or rejection, and does not dispatch to UART or execute shutdown.

## State, limits and shutdown

- `start()` returns when the worker starts, not when broker connection completes.
- `is_connected()` reflects the most recent CONNACK/disconnect state.
- `is_ready()` means connected with all registered filters SUBACKed. With no
  filters it becomes true immediately after connection acceptance.
- The worker exclusively schedules reconnect with delays from 1 to 30 seconds;
  keepalive is 15 seconds for network-loss testing. `reconnect()` requests an earlier waiting attempt,
  without interrupting an active handshake or a healthy connection.
- Subscribe supports 16 filters of up to 255 bytes and QoS 0..2. Publish supports
  up to 4096 payload bytes and 32 pending backend completions; full returns
  `GW_BUSY`. Topic wildcards are rejected for publish.
- `publish()` copies data before returning. `GW_OK` means backend acceptance,
  not broker/subscriber delivery. Disconnected publishes return `GW_NOT_READY`;
  no new offline message is accepted. Pending QoS messages may be retried by
  libmosquitto after reconnect. Stop discards pending outgoing messages.
- Serialize init/start/stop on the application thread. Queries, subscribe and
  publish are mutex-protected after init. Stop joins the worker before freeing
  resources; no callbacks remain afterward. Repeated stop is safe.
- Handshakes have a 15-second application deadline. Blocking DNS and callbacks
  can extend it and delay stop; there is no strict shutdown time bound.
- This module owns process-wide libmosquitto init/cleanup. Adapt that ownership
  before creating other independent libmosquitto clients in the same process.

## Automated loopback tests

```bash
sudo apt install mosquitto
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The tests launch their own broker on a temporary loopback port, never a hosted
service. `mqtt_lifecycle_driver` uses environment overrides in
`tests/mqtt_test_driver.cpp` so manual test settings remain independent.
Tests cover offline startup, reconnect and restored subscriptions, kit routing
at the test layer, arbitrary topics, dynamic wildcard subscriptions, binary
PUB/SUB, retained delivery to the upper layer, invalid inputs and stop/start.
TLS and broker authentication are not covered.

## Select the manual test with one define

Edit only this line in `src/test/mqtt_test.h`, then rebuild and restart:

```cpp
#define MQTT_TEST_MODE MQTT_TEST_BOTH
```

| Value | Behavior |
| --- | --- |
| `MQTT_TEST_CLIENT` | Connect/reconnect, caller-selected SUB/PUB, raw receive logging; no JSON decode |
| `MQTT_TEST_MESSAGES` | Offline: decode six examples and encode one synthetic sample once; no network connection |
| `MQTT_TEST_BOTH` | Both local examples and live MQTT: decode received commands and PUB a synthetic telemetry sample on readiness |

The dispatcher file defines only `mqtt_test_config()` and `mqtt_test_run()`.
The shared run loop sleeps once for 250 ms; child test run functions do not
add another sleep. Main stays unchanged. In messages-only mode, local results
print once and the process remains idle until Ctrl+C.

Combined mode sends the existing ready status to `test/pi-kit01/status` and
an additional example JSON to `test/pi-kit01/telemetry`, once on each transition
to ready, including reconnect. This is synthetic test data, not a live STM32
sample, and it is never sent to production `lab/kit/.../telemetry`. To see both
in the Python GUI, set its receive filter to `test/pi-kit01/#`; clear the
expected-payload field if comparing different schemas is not needed.

The `mqtt_manual_modes` regression compiles the real main and test sources in
all three modes with loopback-only broker settings. It verifies offline-only
behavior, raw receive, live JSON decode, synthetic PUB and clean shutdown.

## Network-loss diagnostics

The manual test no longer prints periodic connected/ready status.
The transport logs connect attempts, CONNACK, disconnect reasons and retry delay.
`connected=0` means the MQTT session is unavailable; it does not specifically
prove that the Wi-Fi adapter is disabled. DNS, broker refusal and timeouts are
reported separately when the backend supplies an error.

The test keepalive is now 15 seconds (MQTT_KEEPALIVE_SECONDS in mqtt_client.cpp).
Silent packet loss typically takes up to about two keepalive intervals to detect
in libmosquitto 2.0.x; OS/DNS blocking and scheduling can extend this. Active
traffic and the point within the current keepalive cycle also affect timing.
No immediate adapter-disconnect notification is promised.

After building, stop the old gateway with Ctrl+C and run ./build/gateway again;
a running process keeps executing the old code even after its file is rebuilt.
Use the transport CONNECT/CONNACK/error lines to inspect reconnect behavior.

The mqtt_network_loss regression test uses a local TCP relay that silently drops
traffic while keeping the downstream TCP socket open. It verifies keepalive
failure detection and resubscription after restoring traffic, without modifying
Windows/WSL adapters or contacting a hosted broker.

## CPU usage on the Pi

The transport still uses one network worker. While connected, libmosquitto waits
for socket activity for up to `MQTT_NETWORK_WAIT_MS` (1000 ms); it returns early
when network data is available. There is no additional 50 ms sleep after each
network iteration. Changing subscriptions may take up to one idle network wait
to be picked up; stopping an idle connected worker can also take about one second,
plus application callback/DNS delays already described above.

While offline, a monotonic condition-variable wait sleeps until the next retry.
Stop or an explicit reconnect request wakes that wait immediately. The manual
test no longer requests redundant reconnects; it checks readiness four times per
second and lets the worker handle all receive callbacks and retry scheduling.
When later integrating UART, give UART its own event/worker path; do not rely on
the manual test's 250 ms sleep as a fast serial-processing loop.

Measured on Ubuntu WSL over a six-second idle interval, the network worker used
about 1.92 ms CPU before and 0.14 ms after this change, with 80 versus 6 context
switches. Offline retry used about 2.62 ms versus 0.27 ms, with 120 versus 2
context switches. These are small absolute costs and host-specific measurements,
not a Pi CPU guarantee. Active traffic, packet rate and synchronous callback/log
work still determine runtime load. Keep callbacks short and hand heavier work
to a bounded queue.

For an actual Pi deployment, build Release and measure the full application:

```bash
cmake -S . -B build-pi -DCMAKE_BUILD_TYPE=Release
cmake --build build-pi --parallel 2
./build-pi/gateway
# In another terminal:
top -H -p $(pidof gateway)
```

`tests/mqtt_idle_profile.py` can compare two saved lifecycle-driver executables
on Linux using local brokers and /proc CPU/context-switch counters. The profiler
is optional and is not part of the gateway runtime.

## Offline UART message tests

In `src/main.cpp`, set `MAIN_USE_UART_TEST` to `1` and `MAIN_USE_MQTT_TEST`
to `0`, then rebuild and run `./build/gateway`. The tests print the six command
frames and PASS/FAIL for CRC, parsing, malformed commands, ACK/NACK and telemetry.
They run once and exit with nonzero status on failure; no MCU or broker is needed.
Set both flags to `0` to run the system, or MQTT=1 / UART=0 for MQTT tests.
The same UART suite is also registered with CTest, including the main selection path.
