# MQTT message codec

Source: `../Giao_thuc_UART_Pi_STM32_MQTT 7ffc80e4c721838aaae5011e9f4eeb31.html`,
sections 3, 5 and 6. This is the document's proposed v1.0 schema. Several JSON
fields and the scale/ruler/shutdown topics remain draft pending server agreement.
The implementation does not claim that the hosted service or STM32 firmware
has been validated against this schema.

Install the C JSON backend on Ubuntu/Pi:

```bash
sudo apt install libjansson-dev
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build -R '^mqtt_messages$' --output-on-failure
./build/mqtt_messages_test
```

Application source remains C++ with function/struct interfaces and English
Doxygen comments. The JSON backend is Jansson >=2.11; no application classes
or handwritten JSON parser are required.

## Web to Pi

Topics must exactly match `lab/kit/{configured-kit}/cmd/{suffix}`. Kit selection
comes from configuration and the actual topic, never from the JSON payload.

| Suffix | Accepted payload | UART ID | Decoded member/value |
| --- | --- | --- | --- |
| `reset` | `{"action":"start"}` / `stop` / `calib` | `0x01` | `data.action` = 1 / 2 / 3 |
| `power` | `{"action":"off"}` / `on` | `0x02` | `data.power_on` = 0 / 1 |
| `voltage` | `{"voltage":9}` | `0x03` | `data.voltage_v`, only 0/3/6/9/12 |
| `scale` | `{"scale":"19.99"}` | `0x04` | `data.scale_code`, "1.999"→0, "19.99"→1, "199.9"→2 |
| `ruler` | `{"position":120}` | `0x05` | `data.ruler_position_mm`, integral 0..300 mm |
| `shutdown` | `{"action":"shutdown"}` | `0x06` | `data.action` = 1 |

Example from an upper-layer receive callback:

```cpp
kit_command_struct_t command = {};
gw_status_enum_t status = mqtt_messages_decode_command(
    "KIT01", topic, payload, length, &command);

if (status == GW_OK)
{
    // Copy command to the application's bounded queue for later dispatch.
    // UART encoding/framing/CRC and shutdown orchestration are separate.
}
```

Validation includes:

- Length-delimited input up to 4096 bytes, no reliance on a NUL terminator.
- Valid UTF-8 JSON object, no trailing data, duplicate keys or embedded NUL.
- Case-sensitive field names; exact scale strings, not float comparisons.
- Finite integral numeric command values; booleans and numeric strings rejected.
  JSON numbers such as `120.0` are accepted when their value is integral.
- Optional JSON `kit_code` must be a string matching the configured/topic kit.
- Unknown additional fields are ignored after the entire JSON is parsed.
- Output remains unchanged on error. Unknown command suffix returns
  `GW_UNSUPPORTED`; bad topic/JSON/type/domain returns `GW_INVALID_ARG`.

Retained policy, duplicate delivery, session freshness and permissions belong
to the upper layer. A decoded shutdown command does not execute any action;
it must go through the future system manager. The decoder has no network or
UART side effects. `mqtt_client` remains generic and does not depend on this
application-specific topic/schema logic.

## STM32 to Pi to JSON

`kit_telemetry_struct_t` is declared once in `src/msg/kit_messages.h`, matching
the design in `../06-module-interfaces.md`. The future UART codec must decode
the 20-byte DATA of a CRC-valid `0x81` frame into this struct, not memcpy the
entire C struct from the wire. This change introduces the shared types only;
UART reception, framing, CRC, ACK tracking and binary decoding are not yet built.

After receiving one actual valid snapshot:

```cpp
char payload[512];
size_t length = 0;
gw_status_enum_t status = mqtt_messages_encode_telemetry(
    "KIT01", received_at_unix_ms, attempt_id, sequence, &sample,
    payload, sizeof(payload), &length);

if (status == GW_OK)
{
    // The upper layer first checks session state, sample age and publish policy.
    status = mqtt_client_publish(
        "lab/kit/KIT01/telemetry", payload, length, 1, false);
}
```

`attempt_id` is copied from the server command that starts the current attempt.
The telemetry JSON contains `kit_code`, `attempt_id`, `seq`, `voltage`,
`current_ma`, `power_on`, `magnetic_field`, `scale` and `ruler_position`; it
does not contain a local timestamp.

For the actual IEEE-754 values in HTML section 5.3, the JSON contains:

```json
{
  "kit_code": "KIT-001",
  "attempt_id": "c2ffb0d4-5901-4c2c-8c4b-141565182757",
  "seq": 0,
  "voltage": 6.070000171661377,
  "current_ma": 125.5,
  "power_on": true,
  "magnetic_field": 123.456789,
  "scale": "199.9",
  "ruler_position": 150
}
```

Serialization uses 17 significant digits to preserve float/double values;
equivalent JSON number spellings are possible. JSON names are exactly the
documented names: `magnetic_field`, not `magnetic_field_mt`. Units remain V,
mA, mT and mm. No conversion to amperes, clamping or LED rounding is performed.

Telemetry checks finite voltage/current, power 0/1, magnetic field 0..200 mT,
scale code 0..2 and ruler position 0..300 mm. Voltage is an actual measurement,
not restricted to command setpoints. Current/voltage hardware limits are still
TBD, so no undocumented range is invented. Invalid samples are rejected whole.

The caller owns the output buffer. On success `written` excludes the NUL; on
failure it is zero and the output is empty when there is buffer space. A small
buffer returns `GW_BUFFER_TOO_SMALL`, never truncated JSON.

Do not serialize UART `0x80` ACK/NACK as telemetry or create a new MQTT ACK topic.
Heartbeat schema is not specified here. Session/maintenance restrictions, 2 Hz
scheduling and dropping stale samples must be handled by the higher layer.

## Manual verification

Select `MQTT_TEST_MODE` in `src/test/mqtt_test.h`. Client-only testing lives in
`mqtt_test_client.cpp`, and codec testing in `mqtt_test_messages.cpp`. The common
`mqtt_test.cpp` retains only config/run dispatch; main remains unchanged.

Messages-only mode runs six local command examples and one local synthetic
telemetry encoding without starting MQTT. Combined mode also decodes live Web
commands and PUBs one synthetic telemetry JSON on `test/pi-kit01/telemetry` when
ready, in addition to the existing status topic. Subscribe Python to
`test/pi-kit01/#` to see both. No synthetic data goes to the production telemetry
topic. These examples never perform UART I/O or hardware actions.

The Python GUI starts with the six example command payloads above. Use
`lab/kit/KIT01/telemetry` only when a real UART telemetry producer is integrated.

`tests/mqtt_messages_test.cpp` covers all command mappings, wrong types/ranges,
wrong kits, duplicate fields, malformed JSON/Unicode, raw non-NUL buffers,
HTML telemetry values and buffer boundaries. Broker/network lifecycle tests
remain separate from codec tests.
