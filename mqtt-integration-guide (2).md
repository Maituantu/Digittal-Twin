# MQTT Integration Guide — Khảo sát từ trường trong ống dây

| Thuộc tính | Giá trị |
| --- | --- |
| Phiên bản guide | 2.14 — thêm công cụ Python DEMO, giữ nguyên contract command V09–V11 đã duyệt |
| Ngày cập nhật | 2026-10-15 |
| Nguồn yêu cầu | [SRS v1.21](SRS.md), đặc biệt §16.17–16.19 và §8–13/17–19; [bàn giao V09–V11](implementation-V09-V11.md) |
| Phạm vi | API server ↔ MQTT broker ↔ kit; liên hệ với UI, streaming và vòng đời lượt |
| Trạng thái | Command power-voltage-scale-ruler-calib đã chốt; chỉ các phần kỹ thuật khác được gắn draft/TBD, chưa nghiệm thu phần cứng/Internet |

## 1. Cách đọc và phạm vi

Guide này thay nội dung giao thức cũ không còn phù hợp với SRS: reset payload rỗng, khóa K, thiếu dòng điện mA và watchdog heartbeat ngoài lượt. Khi khác nhau, yêu cầu đã chốt trong SRS là nguồn ưu tiên; guide không tự đóng các TBD của SRS.

| Nhãn | Ý nghĩa |
| --- | --- |
| **Đã chốt** | Yêu cầu trong SRS; triển khai phải tuân thủ |
| **Draft kỹ thuật** | Quy ước/payload mẫu để hai phía trao đổi; cần thống nhất trước khi implement, không mặc nhiên đã được chủ sản phẩm duyệt |
| **TBD** | Chi tiết còn mở trong SRS §15 hoặc gate của [plan](plan.md); không tự chọn một cách rồi coi là yêu cầu gốc |

Đã chốt topic telemetry/magnetic_field, reset/action start-stop-calib, power/action on-off, voltage0/3/6/9/12 và shutdown payload rỗng0byte. **Xác nhận V08:** start/stop có attempt_id; heartbeat/telemetry echo attempt_id; heartbeat5s chỉ in-use. **Xác nhận V09–V11 ngày 2026-10-14:** chủ sản phẩm đồng ý field voltage, topic/field scale, topic ruler/field ruler_position, attempt_id/command_seq tăng dần trên power-voltage-scale-ruler-calib, QoS1/retain=false/không tự phát lại. Code local đã khớp. Các field/seq kỹ thuật của telemetry/heartbeat vẫn là lựa chọn tích hợp; việc duyệt command không đồng nghĩa các field tín hiệu hoặc firmware/kit/camera/Internet đã được nghiệm thu.

Không hướng dẫn giao thức Socket.IO/signaling chi tiết trong file này. Các kênh đó có contract riêng tại SRS TBD-002/005; không đưa video, form nộp kết quả hoặc điểm vào MQTT chỉ vì chúng liên quan tới lượt thực hành.

API/UI/MQTT adapter và auth/signaling/Python RTC sample đã có triển khai local; kiểm tra cụ thể trong [bàn giao](implementation-V09-V11.md). Chưa có kit/hai camera thật, không nghiệm thu phần cứng/Internet hoặc thuật toán reset-calib.

**Công cụ DEMO 2026-10-15:** [Python simulator](kit-simulator-guide.md) cho nhập kit_code và dùng webcam máy tính, một webcam chia sẻ hai luồng. Chỉ chế độ opt-in này nhận command và phát heartbeat/telemetry tổng hợp theo schema/lifecycle dưới đây; RTC-only/kit thật không bị thay thế. Shutdown chỉ dừng công cụ, không tắt máy tính; không topic/ack/payload riêng, không tự sửa DB, không dùng DEMO nghiệm thu phép đo vật lý.

## 2. Kiến trúc và định danh

```text
Web UI ── API có auth/ownership/validation ── MQTT broker ── Kit
   ▲                                                        │
   └── Socket.IO đúng chủ sở hữu ── API ◄── telemetry/MQTT ───┘

Hai camera tại kit ── WebRTC trực tiếp ── Web UI
     └── WS signaling/auth ── Streaming server ── WS signaling/auth ──┘
```

- Web không kết nối trực tiếp broker để điều khiển kit; mọi lệnh sinh viên phải qua API (FR-DEV-004).
- `kit_code` là mã kit duy nhất, dùng định tuyến MQTT/WebRTC/Socket.IO; không có tên kit (FR-KIT-001).
- Chỉ kit đã đăng ký và còn hợp lệ mới được cấp. Không suy ra một kit mới trong DB từ bản tin MQTT lạ.
- Mã kit không là credential xác thực mạnh. MVP không có quy trình cấp credential riêng từng kit; vẫn phải bảo vệ broker/mạng theo deployment (NFR-SEC-003).
- `kit_code` unique/case-sensitive/tối đa30, regex `^[A-Za-z0-9_-]+$`. Cho phép sửa khi không owner; giữ ID/lịch sử/lịch nhưng về not-ready, admin cập nhật mã trong phần mềm kit rồi xác nhận lại. Không normalize uppercase như account code.
- Video không đi trong telemetry. Mất video độc lập không đồng nghĩa kit MQTT failed (FR-VIDEO-003).

## 3. Danh mục topic

| Hướng | Topic | Công dụng | Mức xác nhận / SRS |
| --- | --- | --- | --- |
| Kit → server | `lab/kit/{kit_code}/telemetry` | Bộ thông số thực tế, 2 mẫu/giây | **Đã chốt**: FR-TEL-001, NFR-INT-002 |
| Kit → server | `lab/kit/{kit_code}/heartbeat` | Tín hiệu sống5s khi kit in-use | Lifecycle/attempt_id/nhịp đã chốt V08; schema seq là lựa chọn code local |
| Server → kit | `lab/kit/{kit_code}/cmd/reset` | Action start/stop/calib | **Đã chốt**: FR-DEV-006 |
| Server → kit | `lab/kit/{kit_code}/cmd/power` | ON/OFF nguồn AC trong lượt, action on/off | **Đã chốt** topic/action/fencing: FR-DEV-001/004, §16.18 |
| Server → kit | `lab/kit/{kit_code}/cmd/voltage` | Field voltage 0/3/6/9/12 V | **Đã chốt**: FR-DEV-001/004, §16.18 |
| Server → kit | `lab/kit/{kit_code}/cmd/scale` | Field scale chuỗi ba thang Tesla | **Đã chốt**: FR-DEV-002/004, §16.18 |
| Server → kit | `lab/kit/{kit_code}/cmd/ruler` | Field ruler_position nguyên 0–300 mm | **Đã chốt**: FR-DEV-003/004, §16.18 |
| Server → kit | `lab/kit/{kit_code}/cmd/shutdown` | Tắt toàn bộ kit do admin yêu cầu | **Đã chốt topic, payload rỗng0byte**; V05 QoS1/retain=false |

Không còn topic điều khiển khóa K trong danh mục hiện hành; không gửi/subscribe command K hoặc yêu cầu trường K trong telemetry.

Topic shutdown riêng với OFF nguồn AC và reset. Hậu tố `/cmd/shutdown` cùng payload rỗng0byte đã xác nhận V05; không JSON action.

## 4. Quy ước payload, delivery và thời gian

### 4.1. Payload

- Reset dùng JSON có trường `action` theo contract đã chốt. Payload command power-voltage-scale-ruler-calib tại §6 đã được duyệt; schema mẫu heartbeat/telemetry ngoài phần đã xác nhận vẫn là thiết kế tích hợp kỹ thuật.
- Số JSON dùng dấu chấm thập phân, không dấu phân tách hàng nghìn hoặc đơn vị trong chuỗi: `123.456789`, không `"123,456789 mT"`.
- Số đo cần là số hữu hạn. Không thay số không hợp lệ bằng 0, giá trị clamp hoặc chuỗi đã format LED.
- Enum thang command là chuỗi `"1.999" | "19.99" | "199.9"` đã chốt, không dùng so sánh float xấp xỉ để phân biệt thang.
- Schema local **không có ts**: dùng giờ nhận server. Heartbeat và telemetry có counter `seq` integer≥0≤Number.MAX_SAFE_INTEGER, tăng riêng theo từng loại trong một attempt; seq≤đã nhận bị bỏ. Reconnect phần mềm kit cần giữ counter của attempt, không reset0 trong cùng lượt. Nếu code kit mất counter cần xử lý integration, không gửi seq lặp để tự gia hạn.
- Topic xác định kit đích. Nếu payload có thêm `kit_code`, phải đối chiếu với mã trong topic; không dùng payload để đổi sang kit khác.
- attempt_id đã được duyệt V08 để chống mẫu/reset của lượt cũ. V09–V11 chốt thêm command_seq tăng dần dùng chung trong lượt cho power-voltage-scale-ruler-calib. Server reserve/commit trước publish, không tái dùng khi transport chưa chắc chắn; gap hợp lệ. Kit chỉ nhận attempt hiện hành và command_seq lớn hơn đã xử lý; QoS1 không có nghĩa exactly-once. Counter command_seq tách biệt counter seq của heartbeat/telemetry.
- Reset start/stop giữ action + attempt_id, không command_seq. Shutdown giữ payload rỗng0byte, không JSON/action/attempt_id/command_seq. Không áp envelope command sinh viên cho hai loại này.

### 4.2. Delivery — command đã chốt, receiver còn phải kiểm chứng

| Loại | QoS | Retain | Mức chốt / điều cần kiểm chứng |
| --- | --- | --- | --- |
| Telemetry | 0 | false | Dữ liệu mới 2 Hz; không replay mẫu cũ như số đo của lượt mới |
| Heartbeat | 0 | false | Mỗi5s/chỉ in-use; attempt_id/seq hợp lệ của lượt hiện tại mới duy trì watchdog |
| Power-voltage-scale-ruler-calib | 1 | false | Đã duyệt; không tự phát lại, fencing attempt_id/command_seq; kiểm chứng kit bỏ lệnh cũ/lặp |
| Reset start/stop, shutdown | 1 | false | Thiết kế transport hiện hành V05/V08; kiểm chứng receiver, không tự thêm command_seq/payload shutdown |

QoS1/retain=false/không tự replay và fencing của năm lệnh điều khiển đã được chủ sản phẩm duyệt. Session sạch/MQTTv5/expiry5s là thiết kế transport code local; QoS tín hiệu, receiver/session/queue và xử lý partial failure còn cần tích hợp/kiểm chứng cùng team kit theo TBD-002/G04. Không tuyên bố exactly-once hoặc an toàn chống replay chỉ từ bảng trên.

- Publish thành công hoặc MQTT transport ack không chứng minh kit đã thực thi hay đạt giá trị vật lý.
- SRS không yêu cầu timeout chờ thực thi/ack cho lệnh sinh viên; không tự thêm UI pending riêng. Ngưỡng mất kit tổng 30 giây là watchdog kết nối, không là command timeout (FR-DEV-005).
- Thiếu reset ack không chặn bắt đầu/cấp tiếp vô hạn nếu kit còn kết nối và thỏa mọi điều kiện khác, **nhưng không bỏ qua nghỉ kit**.
- Lệnh cũ của lượt A không được tác động lượt B. Lệnh shutdown cũ không được phát lại làm tắt kit sau reboot. `retain=false` chỉ là một phần; queue/session/retry và fencing cần thiết kế đầy đủ.
- Không coi `calib` tự nhiên idempotent hoặc tự replay lệnh khi reconnect. Phải thống nhất xử lý lặp/trễ theo ý nghĩa vật lý của action.

### 4.3. Mốc nghiệp vụ do server quản lý

```text
attempt_deadline = min(started_at + max_duration_minutes, group.end_at)
kit_deadline = last_valid_signal_of_current_attempt + 30 giây
student_deadline = student_disconnected_at + 120 giây
cooldown_until = ended_at + rest_seconds
```

Trước tín hiệu kit đầu tiên của lượt, `kit_deadline` tính từ mốc bắt đầu giám sát lượt, không dùng last_seen lúc idle. Các cửa sổ reconnect không kéo dài `attempt_deadline`; `ended_at` là mốc kết thúc hiệu lực, không giờ callback chạy muộn. Thời gian API/DB là UTC (BR-004/006/007/014/015, NFR-REL-001). Khi end, rest_seconds lấy cấu hình mới nhất; cooldown_until đã chốt không tính lại nếu admin sửa khi kit đang nghỉ, chỉ lần nghỉ kế tiếp dùng cấu hình mới.

## 5. Kit → server

### 5.1. Heartbeat và trạng thái kết nối

**Đã chốt V08:** heartbeat mỗi **5 giây**, chỉ khi kit in-use, gồm lúc còn giữ kit trong reconnect của sinh viên. Reset start/stop có attempt_id. Counter seq trong schema code là lựa chọn kỹ thuật chống lặp, không thay watchdog30s.

**Topic/schema code local:** `lab/kit/{kit_code}/heartbeat`.

```json
{
  "kit_code": "KIT-001",
  "attempt_id": "c2ffb0d4-5901-4c2c-8c4b-141565182757",
  "seq": 0
}
```

| Tình huống | Yêu cầu |
| --- | --- |
| In-use, có heartbeat hoặc telemetry hợp lệ của lượt | Cập nhật tín hiệu sống hiện hành; telemetry cũng là tín hiệu hợp lệ |
| In-use, web sinh viên mất kết nối nhưng còn giữ kit | Kit vẫn thuộc lượt, tiếp tục yêu cầu heartbeat; đồng hồ vẫn trừ |
| Idle hoặc đang nghỉ sau lượt | Không yêu cầu heartbeat; im lặng hơn 30 giây không tự offline hoặc gửi Slack |
| Maintenance/inactive | Không gửi heartbeat/telemetry; server giữ trạng thái admin đặt, không tự offline vì thiếu heartbeat |
| Kết thúc lượt, không còn in-use | Dừng yêu cầu heartbeat; không chờ hết thời gian nghỉ mới dừng watchdog |

Kit idle/nghỉ không cần heartbeat/status/LWT và không cần chứng minh connected trước khi cấp. Admin xác nhận sẵn sàng thì được xét cấp theo các điều kiện còn lại. UI không giả online: kết nối là chưa giám sát. Khi cấp và gửi reset start, bắt đầu tổng30s ngay từ lúc cấp; không tín hiệu hợp lệ thì KIT_FAILED/trả quyền/báo admin. Trước mẫu đầu không dùng last_seen idle.

Không tự bỏ yêu cầu heartbeat trong in-use chỉ vì telemetry 2 Hz đã đủ cập nhật last-valid-signal; cách tổ chức/tách bản tin phải thống nhất cùng kit team. Khi telemetry sai schema/ngoài miền, không được dùng nó như số đo hợp lệ; chính sách phân biệt signal sống và mẫu đo lỗi còn cần chốt.

### 5.2. Telemetry

**Topic đã chốt:** `lab/kit/{kit_code}/telemetry`. **Tần suất:** 2 Hz khi thực hành; maintenance/inactive không phát. Quy tắc phát telemetry lúc idle/đang nghỉ chưa được SRS chốt; không dùng im lặng ngoài in-use để tạo lỗi heartbeat.

**Schema code đang dùng local:** magnetic_field/attempt_id theo quyết định đã chốt; tên các trường còn lại là lựa chọn tích hợp cần đồng bộ kit. current_ma là mA, không đổi sang A. Schema strict: thiếu trường/field dư (kể cả ts/k_lock)/seq lặp/topic kit mismatch/retained bị bỏ và không duy trì watchdog.

```json
{
  "kit_code": "KIT-001",
  "attempt_id": "c2ffb0d4-5901-4c2c-8c4b-141565182757",
  "seq": 0,
  "voltage": 6.07,
  "current_ma": 125.5,
  "power_on": true,
  "magnetic_field": 123.456789,
  "scale": "199.9",
  "ruler_position": 150
}
```

| Trường trong draft | Kiểu | Miền/đơn vị và ý nghĩa |
| --- | --- | --- |
| `kit_code` | string | Mã kit trùng topic; không phải tên kit hoặc credential |
| `attempt_id` | UUID string | Đúng lượt đang giữ kit; không nhận mẫu của lượt khác |
| `seq` | integer≥0 | Tăng riêng theo heartbeat/telemetry/attempt; mẫu lặp/cũ bị bỏ |
| `voltage` | number | Điện áp **đo thực tế**, V; không ràng buộc bằng enum mức nguồn yêu cầu. SRS chưa chốt min/max/độ chính xác phép đo |
| `current_ma` | number | Dòng điện **đo thực tế**, mA; tên wire/domain/độ chính xác đo còn TBD-002. LED nguồn chỉ render cắt bốn chữ số đầu, 852.34→852.3, không làm tròn/không sửa payload |
| `power_on` | boolean | Trạng thái nguồn AC thực tế |
| `magnetic_field` | number | Từ trường Tesla Meter, **0–200 mT, không âm**; giữ số gốc nhiều chữ số thập phân |
| `scale` | string | Thang hiện tại `"1.999"`, `"19.99"`, `"199.9"`, đơn vị mT |
| `ruler_position` | integer | Vị trí tuyệt đối **0–300 mm**, bước 1 mm |

Các quy tắc bắt buộc (FR-DEV-001/002/003, FR-TEL-001/002, AC-TEL-001…003):

1. Không có trường K bắt buộc; không dùng dữ liệu K để điều khiển UI mới.
2. Điện áp đo khác mức điện áp yêu cầu. Ví dụ `6.07 V` không bị từ chối chỉ vì lệnh chọn mức nguồn dùng `0/3/6/9/12 V`.
3. Dòng điện đi xuyên suốt kit → server → Socket.IO → UI bằng **mA**. Không tự nhân/chia 1000, không tự tính từ điện áp hoặc thông số ống dây.
4. Bốn LED AC POWER SUPPLY hiển thị dòng điện mA, không điện áp; cắt bốn chữ số đầu/không làm tròn, 852.34→852.3. Chỉ overflow phần nguyên >4 chữ số/domain đo còn TBD-002, không hỏi lại lựa chọn định dạng đã chốt.
5. Màn hình bốn LED Tesla Meter hiển thị từ trường mT. Chấp nhận 0, 200 và số như `123.456789`; dữ liệu âm hoặc >200 không hợp lệ, không tự clamp. Không coi ký hiệu thang `199.9` là lý do đổi miền magnetic_field đã chốt thành ≤199.9.
6. **TBD-015 đã đóng:** LED Tesla cắt bỏ phần thập phân dư để vừa tối đa bốn chữ số, không tính dấu chấm, không làm tròn; giữ phần nguyên và số đo `magnetic_field` gốc trên MQTT/server/Socket.IO. Ví dụ 123.456789 mT hiển thị 123.4 mT; đây chỉ là render LED, không cắt số sinh viên nhập hoặc dữ liệu đã lưu. Không đổi thang/dải telemetry hoặc dùng số form thay số kit.
7. Một mẫu phải nhất quán, không ghép trường từ các thời điểm khác nhau. Không ghi từng mẫu vào DB hoặc tạo history telemetry để vẽ chart. Đồ thị phần 2 dùng số liệu sinh viên chủ động lưu qua form, không phải stream MQTT.
8. Chỉ gửi dữ liệu đo tới đúng chủ sở hữu lượt hiện hành. Chưa có mẫu đầu thì UI hiển thị `—` disabled. Không lấy mẫu idle/lượt cũ làm mẫu đầu của lượt mới.
9. Sau API điều khiển thành công, UI giữ số đo/trạng thái cũ đến khi kit gửi telemetry mới; không optimistic update giá trị thực tế.
10. Theo xác nhận UI mới, góc núm nguồn/mốc active chọn **mốc gần nhất** với điện áp đo trong tập 0/3/6/9/12 V, ví dụ 6.07 V → mốc6 V/xanh lá. Không sửa voltage gốc, không yêu cầu thêm field mức đặt và không tự publish lệnh ép về mốc. Tie cách đều chọn mốc thấp hơn (4.5 V→3 V), đồng nhất núm/tour/server chọn U lưu. Núm range theo thang telemetry; LED Tesla vẫn từ magnetic_field, không lấy B₀ form nhập. Chi tiết ở [guide UI](practice-ui-guide.md).

## 6. Server → kit

### 6.1. Reset start / stop / calib — đã chốt

**Topic:** `lab/kit/{kit_code}/cmd/reset`. JSON bắt buộc có `action` với đúng một trong `start | stop | calib`; payload reset rỗng không còn là contract hiện hành.

**Đầu lượt:**

```json
{"action":"start","attempt_id":"c2ffb0d4-5901-4c2c-8c4b-141565182757"}
```

**Cuối lượt:**

```json
{"action":"stop","attempt_id":"c2ffb0d4-5901-4c2c-8c4b-141565182757"}
```

**Sinh viên hiệu chuẩn trong lượt:**

```json
{"action":"calib","attempt_id":"c2ffb0d4-5901-4c2c-8c4b-141565182757","command_seq":1}
```

| Action | Khi gửi / quyền | Tác động nghiệp vụ |
| --- | --- | --- |
| `start` | Server tự gửi khi cấp kit, tạo lượt ACTIVE và chiếm kit in-use | Đồng hồ bắt đầu ngay khi nhận kit, không đợi ack; bắt đầu giám sát kit cho lượt mới |
| `stop` | Server tự gửi khi kết thúc mọi nguyên nhân, khi có thể | Thu hồi ownership một lần, áp dụng nghỉ từ ended_at; không đợi ack để tính nghỉ |
| `calib` | Nút sinh viên, API kiểm tra chủ sở hữu/ACTIVE/chưa hết hạn/connected; khóa khi reconnect/end | Không tạo lượt, không đổi timer/deadline/quyền hoàn thành hoặc giải phóng kit; UI chờ telemetry |

**TBD-003 đã đóng về trách nhiệm:** Kit chịu trách nhiệm ý nghĩa vật lý, quy trình an toàn và thực thi từng action `start | stop | calib`. Hệ thống chỉ auth/validate/định tuyến và gửi message MQTT đúng topic/action; không triển khai hoặc tự suy đoán thuật toán reset/hiệu chuẩn, giá trị nguồn/thang/thước sau reset hay gán từ trường bằng 0. FE chỉ cập nhật từ telemetry thực tế, không coi publish thành công là đã thực thi. Start/stop phục vụ trạng thái an toàn do kit đảm nhận; không bắt hệ thống định nghĩa GPIO/giá trị đích/thuật toán calib để gửi command. G05 chỉ còn phần tích hợp phần cứng khác như driver/camera/shutdown khi liên quan, không mở lại trách nhiệm đã chốt.

Thiếu ack khi kit còn connected không phải lý do chặn nhận lượt/cấp tiếp vô hạn. Vẫn phải kiểm tra thời gian nghỉ, ready, cờ lỗi, quyền và lịch. Shutdown phối hợp stop trước tắt nếu thực hiện được, không trì hoãn shutdown để đợi ack và không gửi reset muộn sau tắt.

### 6.2. ON/OFF nguồn AC — contract đã chốt

**Topic đã chốt:** `lab/kit/{kit_code}/cmd/power`.

```json
{"action":"on","attempt_id":"c2ffb0d4-5901-4c2c-8c4b-141565182757","command_seq":2}
```

```json
{"action":"off","attempt_id":"c2ffb0d4-5901-4c2c-8c4b-141565182757","command_seq":3}
```

Miền action đã chốt: `on | off`. UI là button ON xanh lam phía trên/OFF đỏ phía dưới, hover highlight đúng element. Đây là hai nút nguồn AC trong lượt, **không phải shutdown toàn bộ kit**. OFF nguồn AC không tự end lượt, trả ownership hoặc chuyển kit inactive. Web gọi API, không publish trực tiếp; chờ telemetry xác nhận trạng thái nguồn thực tế (FR-DEV-001/004/005).

### 6.3. Chọn mức điện áp — contract đã chốt

**Topic đã chốt:** `lab/kit/{kit_code}/cmd/voltage`, field `voltage` và attempt_id/command_seq theo §4.1.

```json
{"voltage":9,"attempt_id":"c2ffb0d4-5901-4c2c-8c4b-141565182757","command_seq":4}
```

API chỉ chấp nhận mức **0, 3, 6, 9, 12 V**. Giá trị khác, ví dụ 15, bị từ chối trước publish. Telemetry `voltage` là phép đo thực tế, không bắt buộc bằng giá trị lệnh và không tự làm đổi dòng điện trên UI (FR-DEV-001, AC-DEV-001, AC-TEL-002).

Núm/mốc hover xanh lam, click gọi API; chưa telemetry không xoay núm/xác nhận active. Telemetry về thì UI chọn mốc gần điện áp đo nhất (6.07 V → 6 V); cách đều chọn mốc thấp hơn (4.5 V → 3 V), áp dụng cho núm/tour và server chọn mốc lưu Phần 3. Góc núm/mốc active xanh lá; giữ điện áp gốc, không làm tròn số đo hoặc phát lệnh điều khiển ngầm.

### 6.4. Chọn thang Tesla Meter — contract đã chốt

**Topic đã chốt:** `lab/kit/{kit_code}/cmd/scale`, field `scale` chuỗi và attempt_id/command_seq theo §4.1.

```json
{"scale":"19.99","attempt_id":"c2ffb0d4-5901-4c2c-8c4b-141565182757","command_seq":5}
```

Miền thang: **`"1.999" | "19.99" | "199.9"` mT**. Không tự thêm dấu âm/miền ± hoặc command K. Đổi thang không tự suy ra số đo mới; UI giữ dữ liệu cũ cho đến telemetry. LED cắt phần thập phân dư để vừa bốn chữ số, không làm tròn/không sửa magnetic_field gốc (TBD-015 đóng). Không tự suy ra phép đo từ ký hiệu thang hoặc thêm clamp/command để sửa số đo (FR-DEV-002).

Núm range dùng tương tác hover/click giống núm điện áp, active xanh lá/góc núm theo thang telemetry. Không cần chọn gần nhất cho enum thang, không dùng số B₀ nhập tay để xoay núm hoặc cập nhật LED.

### 6.5. Vị trí thước — contract đã chốt

**Topic đã chốt:** `lab/kit/{kit_code}/cmd/ruler`, field `ruler_position` và attempt_id/command_seq theo §4.1.

```json
{"ruler_position":120,"attempt_id":"c2ffb0d4-5901-4c2c-8c4b-141565182757","command_seq":6}
```

- `ruler_position` là vị trí tuyệt đối **0–300 mm**, số nguyên/bước1mm, không là delta. Field đã được chủ sản phẩm duyệt V09–V11; phần mềm kit riêng cần triển khai/kiểm chứng đúng contract.
- Với thao tác kéo tay nắm/slider, chỉ gửi khi thả chuột; không phát lệnh liên tục theo từng mousemove. Slider hiện khi hover tay nắm và vẫn thao tác được khi con trỏ di chuyển sang slider.
- Hai nút trái/phải cộng dồn ±1 mm trên mục tiêu gần nhất/clamp 0–300; gom click trong khoảng 150 ms rồi gọi API một vị trí tuyệt đối cuối, không gửi ngay từng click hoặc chờ telemetry mới được bấm tiếp. Không topic delta hoặc queue lệnh theo pixel. Actual vẫn telemetry, nhãn “Đang yêu cầu: … mm” riêng đến khi kit xác nhận; reconnect/end hủy batch chưa gửi, không timeout/auto retry mới. Fencing attempt_id/command_seq và QoS1/retain=false/không replay đã chốt; còn kiểm chứng receiver/partial failure, không thay wire chỉ vì gom thao tác FE.
- -1, 301 hoặc 120.5 bị API từ chối trước publish.
- Hình kéo tạm không được thể hiện như giá trị đo đã xác nhận. Thước thực tế và từ trường cập nhật từ telemetry (FR-DEV-003, AC-DEV-001/002).
- Label/vạch thước dùng mm từ telemetry; drag/click CTA không đồng thời pan scene. Zoom/pan/ẩn-hiện streaming là thay đổi FE, không publish MQTT.

### 6.6. Shutdown toàn bộ kit — topic và payload đã chốt

**Topic đã chốt:** `lab/kit/{kit_code}/cmd/shutdown`.

**Payload shutdown:** rỗng0byte; không `{}`, không JSON, không action. Phần mềm kit riêng ngoài repo nhận MQTT và tắt toàn bộ máy; Python chỉ mẫu RTC, không gọi OS shutdown.

Không gửi chuỗi `null`, khoảng trắng, `{}` hoặc `{"action":"shutdown"}`; payload MQTT có độ dài đúng 0.

FR-KIT-008/AC-KIT-003/004 và FL-016 đã chốt:

1. Chỉ admin gọi action shutdown. Cho phép yêu cầu ở cả `offline`, `not-ready`, `ready`, `in-use`, `error`, `maintenance`, `inactive`; không tái sử dụng guard lệnh sinh viên để bắt ready/owner. Vẫn kiểm tra quyền và kit đích hợp lệ.
2. Lệnh yêu cầu **tắt nguồn toàn bộ kit**, dừng MQTT và hai nguồn video. Không thay bằng OFF nguồn AC hoặc reset. Phần mềm kit riêng chịu trách nhiệm tắt nguồn thật; không giả mô phỏng thành công.
3. Sau khi gửi lệnh, cập nhật DB/UI thành `inactive`. Publish được chỉ báo **“Đã gửi lệnh shutdown”**, không báo đã tắt vật lý, đặc biệt khi kit offline. Chính sách delivery tới kit offline còn TBD-002; không mặc nhiên hẹn lệnh tắt cho lần reboot sau.
4. Nếu kit có owner, kể cả reconnect, kết thúc ngay `ADMIN_TERMINATED`, trả quyền làm lại, thu hồi control/socket/video và free ownership. Không chờ 30s/120s, không cấp kit đó cho người kế tiếp dù đã free hoặc hết nghỉ.
5. Giữ assignment/lịch/timeline tương lai. Shutdown không soft delete kit hoặc gỡ reservation. Bật kit lại vẫn `inactive` đến khi admin kích hoạt/xác nhận ready (không yêu cầu chứng minh kết nối khi idle).
6. Không tạo `KIT_FAILED` hoặc Slack sự cố chỉ vì kit chủ động shutdown ngừng phát. Callback/watchdog/reset cũ không ghi đè inactive hoặc tác động lượt khác.
7. V05 ghi interlock `shutdown_pending` trước publish để chặn claim/mode/rename; publish QoS1/retain=false qua MQTTv5 session sạch riêng, expiry5s, không reconnect/replay tự động. Broker ack→DB inactive. Lỗi publish/chưa rõ hoặc DB finalize lỗi báo riêng, giữ chặn; admin thử lại sau10s với requestId HTTP. RequestId không phải payload MQTT. Receiver phải session sạch/không lưu lệnh cũ; QoS1 không chứng minh thực thi exactly-once. Không hẹn shutdown để tắt máy lần reboot sau.

## 7. Vòng đời lượt, reconnect và nghỉ kit

### 7.1. Cấp lượt → kết thúc → nghỉ → cấp tiếp

| Bước | Server/kit phải thực hiện | Không được làm |
| --- | --- | --- |
| Claim | Kiểm tra nguyên tử quyền sinh viên, lịch, kit thuộc nhóm/ready/không cờ chặn/không owner/đã hết nghỉ | Không cấp chỉ vì nhận thông báo available cũ |
| Bắt đầu | Tạo ACTIVE, started_at/deadline, chiếm in-use, gửi reset start; giám sát tín hiệu của lượt mới | Không đợi reset ack mới tính giờ; không dùng last_seen cũ để end ngay |
| Trong lượt | Telemetry 2 Hz, heartbeat khi in-use; API kiểm tra owner/deadline trước mỗi lệnh/calib | Không cho hai sinh viên điều khiển một kit; không đổi UI theo API ack |
| Kết thúc | Commit một terminal state, lưu ended_at hiệu lực, revoke control/socket/video/submit, stop khi có thể, free đúng owner | End/callback cũ không reset/free kit của lượt mới |
| Nghỉ | Khi end lấy rest_seconds hiện hành, chốt/lưu cooldown_until = ended_at + rest_seconds; không heartbeat khi không in-use | Cấu hình nguyên≥0/default 0/không max nghiệp vụ. Đổi khi đang dùng áp dụng lần nghỉ sau lượt; đổi khi đang nghỉ giữ cooldown cũ/lần nghỉ tiếp mới dùng R mới. End lặp/restart/ready không tính lại hoặc bypass |
| Hết nghỉ | Recheck ready/cờ lỗi/lịch/ca/quyền rồi mới thông báo available; claim kiểm tra lại | Không tự ready kit lỗi, not-ready, maintenance hoặc inactive |

Nghỉ áp dụng cả `COMPLETED`, `TIME_EXPIRED`, `STUDENT_DISCONNECTED`, `KIT_FAILED`, `ADMIN_TERMINATED`. Cho phép **0 giây**, không âm; R=0 chỉ bỏ thời gian chờ, không bỏ các điều kiện cấp. Server tính nghỉ từ ended_at, không lúc stop ack hoặc callback chạy muộn; end lặp không khởi động lại nghỉ.

Thời gian nghỉ là điều kiện độc lập, không thêm trạng thái kit thứ tám. `rest_seconds` là số nguyên không âm, mặc định 0, không có giới hạn trên về nghiệp vụ; từ chối giây lẻ/số âm. Sửa khi kit đang được sử dụng: lần nghỉ ngay sau lượt đó dùng cấu hình mới nhất tại thời điểm kết thúc. Sửa khi kit đang nghỉ: giữ nguyên `cooldown_until` đã chốt, cấu hình mới áp dụng từ lần nghỉ kế tiếp. Server chốt/lưu mốc `ended_at + rest_seconds` trong xử lý end, không tính lại mốc nghỉ cũ bằng cấu hình mới sau refresh/restart hoặc end lặp. Không tự nới reservation hoặc coi free ownership là timeline đã được gỡ (BR-015, FR-KIT-009, AC-KIT-006); TBD-013/G12 đã đóng.

### 7.2. Kit mất kết nối trong in-use

1. Theo dõi heartbeat/telemetry hợp lệ của lượt. Nếu phát hiện disconnect sớm, chuyển UI sang **“Đang kết nối lại với kit”**, khóa điều khiển/calib, giữ ownership/số đo cũ và tiếp tục đếm giờ; video còn hoạt động vẫn hiển thị.
2. Chỉ có **một hạn tổng 30 giây** tính từ tín hiệu hợp lệ cuối của lượt; trước mẫu đầu tính từ mốc bắt đầu giám sát. Không đợi phát hiện thiếu heartbeat 30 giây rồi chờ thêm 30 giây nữa.
3. Phục hồi trước hạn và lượt/ca còn hiệu lực → tiếp tục đúng lượt, không reset timer hoặc cấp cho người khác.
4. Hạn kit đến trước hạn lượt/ca và chưa có tín hiệu → `KIT_FAILED`, trả quyền làm lại, đóng quyền truy cập, free ownership, áp nghỉ, cảnh báo admin/Slack. Kit quay lại sau hạn thành `not-ready` đến khi admin xác nhận, không tự ready.
5. Nếu hạn kit **trùng chính xác** hạn lượt/ca → **`TIME_EXPIRED`**, tính Đã làm, không được làm lại; kit vẫn không đủ điều kiện cấp khi mất kết nối. Nếu các hạn khác nhau, xử lý mốc hiệu lực trước, không theo thứ tự job muộn.

Ngoài in-use không có flow “im lặng heartbeat → offline sau30s → Slack”. Không bắt buộc topic status/LWT/heartbeat lúc idle; giám sát chỉ bắt đầu từ lần cấp/start mới.

### 7.3. Phân biệt lỗi kit, web và video

| Nguồn gián đoạn | Hành vi |
| --- | --- |
| MQTT/kit trong lượt | Tổng 30s; trước hạn giữ owner, quá hạn hiệu lực có thể KIT_FAILED như §7.2 |
| Web sinh viên | Giữ lượt/kit tối đa 120s, không vượt hạn ca/lượt; kit vẫn in-use và yêu cầu heartbeat. Hết hạn là STUDENT_DISCONNECTED/tính Đã làm, trừ tie với kit trước hạn lượt/ca ưu tiên KIT_FAILED |
| Một/hai camera hoặc streaming-only | Box lỗi tương ứng; API/MQTT/web điều khiển còn hoạt động thì lượt tiếp tục, không suy ra KIT_FAILED |
| Shutdown hoặc admin action cần thu hồi | Kết thúc ngay theo SRS; không chờ cửa sổ kit/web để free owner |

Timer không dừng khi reconnect. **TBD-010/G07 đã đóng:** Nếu deadline web 120 giây và kit 30 giây trùng nhau trước hạn cá nhân/ca, ưu tiên `KIT_FAILED`: không tính hoàn thành, trả quyền làm lại. Nếu kit deadline trùng hạn cá nhân/ca (kể cả trùng cả ba), giữ ưu tiên `TIME_EXPIRED`, tính Đã làm/không làm lại. Khi các mốc khác nhau, xử lý mốc hiệu lực sớm nhất, không theo thứ tự callback; một terminal state/idempotent.

## 8. Validation, định tuyến và trạng thái an toàn

### 8.1. Lệnh sinh viên

API phải kiểm tra tại lúc xử lý, không chỉ tin nút đã enabled ở FE:

- Access token/session/account hiện hành, đúng sinh viên sở hữu lượt/kit.
- Lượt ACTIVE, chưa quá deadline server, còn quyền truy cập; scheduler chưa chạy không làm request quá hạn trở nên hợp lệ.
- Kit còn connected, không trong reconnect; request sai owner/end/revoke bị từ chối trước publish.
- Đúng loại action và miền mức nguồn/thang/vị trí. Trong nhóm reset action, sinh viên chỉ được gọi calib, không tự phát start/stop; shutdown không thuộc API điều khiển sinh viên.
- Không giữ lệnh trong retry để thực thi cho owner mới sau reconnect/end.

Shutdown admin áp dụng guard riêng ở §6.6. Start/stop là thao tác vòng đời server, không phải thao tác sinh viên được chọn tùy ý. Endpoint/status/error code cụ thể còn TBD-001; không coi bảng 401/403/422 trong guide cũ là contract đã duyệt.

### 8.2. Bản tin kit

- Validate kit đã đăng ký, topic/payload nhất quán, JSON/schema/miền dữ liệu và độ mới theo contract đã chốt.
- Không coi mẫu lỗi/ngoài miền là số đo hợp lệ, không tự clamp/lấy giá trị 0 rồi cập nhật UI. Cách báo/từ chối mẫu lỗi và việc nó có duy trì signal sống hay không cần chốt TBD-002.
- Không broadcast telemetry toàn hệ thống. Server định tuyến theo owner hiện hành, không theo student/attempt tùy ý do kit gửi.
- Bản tin muộn sau end/shutdown không mở lại lượt, cập nhật quyền điều khiển, tự ready hoặc ghi đè inactive/maintenance.
- Sau restart, đối chiếu ownership, hạn lượt/reconnect và cooldown trước mở lại điều khiển/cấp lượt; không replay reset/command cũ một cách tự động.

### 8.3. Báo lỗi và Slack không phải topic điều khiển mới

Sinh viên báo kit có vấn đề qua **API** với loại lỗi text tự do tối đa100 và mô tả tối đa1000 ký tự, bắt buộc sau trim, không qua telemetry. Lượt hiện tại tiếp tục; chặn cấp lượt kế tiếp cho đến xử lý. Chưa xử lý tới cuối lượt → not-ready; admin cho tiếp tục gỡ cờ nhưng vẫn cần nghỉ đủ trước cấp lại (FR-ALERT-001/002).

Slack qua webhook env: kit lỗi/mất kết nối hết ngưỡng, sinh viên báo lỗi, admin xác nhận ready trở lại, thiếu công suất tại đầu ca. Một cảnh báo/sự cố và một tin phục hồi khi admin xác nhận; thiếu công suất chỉ một cảnh báo cho ca tại thời điểm bắt đầu. Không spam mỗi heartbeat hoặc timer tick, không lưu lịch sử báo lỗi/thông báo vào DB. Shutdown chủ động không tự tạo cảnh báo sự cố (FR-ALERT-003, FR-SCHED-005).

## 9. Telemetry và kết quả sinh viên nhập là hai nguồn dữ liệu riêng

| Dữ liệu | Nguồn / lưu trữ | Quyền và quan hệ với MQTT |
| --- | --- | --- |
| Telemetry | Mẫu đo kit 2 Hz, không ghi toàn bộ lịch sử DB | Server → Socket.IO đúng owner, số đo thực tế cho UI |
| Kết quả phần 2 | Vị trí nguyên 0–30 cm/B₀ mT nhập tay, không khớp telemetry; cùng lượt/phần/vị trí **ghi đè**, tối đa 31 điểm hiện hành | Insert/update chỉ ACTIVE/chưa hết hạn, cấm sau end. B₀ theo ví dụ bốn chữ số SRS, nhận cả 9999 nhưng **telemetry magnetic_field vẫn 0–200 mT**. Cm của bài nộp không đổi command/telemetry/label mm |
| Kết quả Phần 3 | Nhập tay I **A**/B₀ mT cho cả năm U0/3/6/9/12V; server chọn mốc gần telemetry hợp lệ nhất, tie chọn thấp, cùng mốc ghi đè/giữ số đo gốc | Telemetry/LED dòng điện vẫn **mA**. Chart hồi quy B₀=kI ép qua (0,0), không I₀/record đo giả. Form/clear/chart/popup không phát MQTT. Guard owner/ACTIVE/deadline; clear Phần 2 giữ Phần 3/khóa save đến đủ 31 lại |
| Điểm | GV lớp/GV nhóm đúng phạm vi chấm một điểm chung nguyên 0–10 cho mọi submit của một lượt | Gắn attempt_id, không điểm từng record/gộp lượt; GV nhóm chịu trách nhiệm chính, SV không xem điểm; không đưa vào telemetry/MQTT |

Form/grid/chart/điểm không là MQTT command. **Không còn Snapshot riêng**; form/dataset thay vai trò ghi số liệu, không endpoint/entity/topic Snapshot. Hai tab có dataset riêng; Xóa đồ thị clear grid/DB tab active theo soft delete, có confirm và chỉ khi ACTIVE/chưa hết hạn. Không reset/MQTT hoặc đổi ownership; server từ chối save/clear sau end, kể cả request trễ. Sau end SV chỉ đọc grid/chart mình theo quyền hiển thị, không điểm/intro/tooltip/popup hoàn thành hoặc live media/telemetry. GV xem dataset hiện hành đúng scope, không lịch sử ghi đè. Tour ON trước 3V chờ telemetry; mẫu hợp lệ đã thỏa thì tự qua, không lệnh/change giả; telemetry vẫn 2 Hz. Fullscreen/layout/tour/đồ thị không đổi timer. Chi tiết FR-SUB-001…006, FR-UI-007…011 và FR-GRADE-001/002; các TBD còn mở không tự chốt trong guide MQTT.

Toolbar bên phải có **Full màn hình (fullscreen thật trình duyệt), Calib, Đổi layout, Báo kit lỗi, Rời khỏi bài thực hành**, icon/label phía dưới/xếp dọc. Fullscreen/đổi layout không phát MQTT hoặc đổi lượt; **Calib qua API → `lab/kit/{kit_code}/cmd/reset` payload `{"action":"calib"}`**, không payload rỗng/start/stop, không tự đặt số đo 0. Guard owner/ACTIVE/deadline/reconnect cho calib vẫn áp dụng. Không thêm topic chỉ vì đổi vị trí nút trên UI. **Rời khỏi bài thực hành** mở confirm; mở/hủy không phát MQTT, xác nhận gọi API end hiện hành/COMPLETED rồi server gửi reset action `stop` khi có thể, thu hồi/free/nghỉ kit. Không yêu cầu connected như calib/không thêm cmd leave hoặc shutdown. **Bộ đếm còn lại** lấy hạn/giờ server, không telemetry; hết giờ server TIME_EXPIRED/stop/free/nghỉ ngay, popup duy nhất **“xác nhận và rời khỏi bài thực hành”** chỉ xác nhận/điều hướng, không stop/reset/release lần nữa. Popup/tour/chart/retry không pause/gia hạn hoặc giữ kit. **Báo kit lỗi** mở form loại lỗi text tự do và mô tả, đều bắt buộc sau trim/loại≤100/mô tả≤1000; gọi API hiện hành → admin toast/Slack, không topic MQTT mới. Lượt hiện tại tiếp tục/chặn cấp tiếp như FR-ALERT. Không chức năng Snapshot.

**Đồng bộ SRS v1.17:** gate đủ 31 Phần 2, đóng popup hoặc đóng chart mở từ popup Phần 2 tự chuyển Phần 3. Hướng dẫn Phần 3 skip được, không bypass gate; nếu không skip chờ telemetry 150 mm, sau đó không khóa thước, tooltip 3V click tắt. Form I A/B₀ mT nhập tay cả năm mốc, chỉ số/dấu chấm, chung rule bốn chữ số; không dấu phẩy/chữ/e/dấu âm, giữ ngoại lệ 0.0123 hợp lệ. Wire/LED vẫn mA, không tự đổi unit. Mốc U do server chọn gần telemetry nhất, tie thấp (4.5→3V), không FE chọn. Click grid Phần 2 điền form, Phần 3 chỉ xem số liệu/không thay mốc lưu. Chart Phần 3 hồi quy qua gốc B₀=kI, k=Σ(I·B₀)/Σ(I²), không record đo giả; nếu Σ(I²)=0 chưa tính đường, không chia 0. Thêm ô thiếu làm đầy mới popup, ghi đè không popup; refresh đủ trong lượt vẫn hiện lại. Đủ lại Phần 2 vẫn có popup dù Phần 3 đủ, popup Phần 2 trước; đóng popup hoặc xem rồi đóng chart → tự sang Phần 3 → hiện popup toàn bài nếu vẫn đủ. Tour Phần 3 một lần mỗi lần tải trang, không lặp đổi tab/refresh chạy lại/luôn Skip; không tour/popup đọc lại sau end. TBD-016 đã đóng: form cả hai phần ở phải/mép trên25vh, không Back/Replay, spotlight tự căn zoom/pan; các thay đổi này không MQTT/reset. .5/1. bị từ chối, 0.5/1.0/số nguyên hợp lệ theo rule cũ. Chấm mới/sửa chỉ sau end và khi lớp INPROGRESS, nhiều GV một điểm chung, CSV tám cột có điểm/duration hh:mm:ss; không lộ điểm SV. Clear Phần 2 giữ/khóa save Phần 3 đến đủ lại. Popup không tự end/stop/nhả kit/dừng heartbeat/timer; chỉ action kết thúc theo lifecycle hiện hành. LED dòng điện mA cắt bốn chữ số đầu, 852.34→852.3, chỉ render/không giảm chính xác payload. Tesla LED đã chốt cắt số thừa/không làm tròn, TBD-015 đóng. Không topic/payload mới cho các thay đổi UI này.

## 10. Cấu hình môi trường và giới hạn triển khai

- `MQTT_URL` dùng cho API trong [env mẫu](../.env.example); host/cổng phải đúng mạng Compose và mạng kit. `KIT_MQTT_URL` là field legacy V00 tùy chọn, không bắt buộc để chạy Python RTC sample; phần mềm kit riêng cấu hình broker của nó. Không đưa secret broker/Slack vào FE bundle hoặc payload nghiệp vụ.
- Demo ứng dụng dùng HTTP/WS theo SRS; điều này không tự quyết transport/TLS/ACL broker MQTT. Guide cũ ghi MQTT TLS như bắt buộc không còn được dùng làm yêu cầu đã chốt. Thiết kế broker/network cần chốt theo topology thực, không coi demo là production an toàn.
- Broker anonymous/bind loopback trong cấu hình local không là cấu hình được phép public nguyên trạng lên Internet. Không tự thêm quy trình credential riêng từng kit vào MVP; bảo vệ broker theo NFR-SEC-003.
- Đã chốt vai trò: phần mềm kit riêng ngoài repo publish telemetry/heartbeat và subscribe/thực thi command/shutdown. Python chỉ mẫu tích hợp camera lên RTC server, không MQTT runtime/driver/shutdown OS. Các factory MQTT/device V00 giữ nguyên, không tự kích hoạt hoặc phát số liệu giả.
- Mục tiêu tải: 2 nhóm đồng thời, tối đa 10 kit/10 sinh viên đang thực hành; telemetry 2 Hz, đường kit → UI mục tiêu ≤1s trong mạng bình thường. Không thêm SLA video/FPS/độ phân giải (NFR-PERF-001/002).
- V05 đã đồng bộ fixture/schema sang `magnetic_field`, có `current_ma`, bỏ `k_on`. Các field mẫu còn lại là draft, chưa là wire contract active đã nghiệm thu. Mã kit dùng cùng regex/case/giới hạn ở API/Web/stream/Python. Không dùng fixture làm telemetry thật.

## 11. Checklist tích hợp và nghiệm thu

Các mục dưới đây là ca smoke/manual/integration **cần thực hiện**, không phải kết quả đã pass. Không tự tạo unit test mới theo rule repository. Dùng kit/test broker được cho phép; không phát shutdown hoặc thay đổi nguồn thiết bị thật chỉ để kiểm tra tài liệu.

| Ca | Kết quả cần chứng minh | SRS |
| --- | --- | --- |
| Bắt đầu/kết thúc/calib | Đúng topic reset/action; calib sai owner/reconnect/end bị chặn, không đổi timer; thiếu ack không bỏ nghỉ | AC-DEV-003/004 |
| Telemetry nguồn | Điện áp đo độc lập mức đặt, dòng điện giữ mA tới UI; LED nguồn hiển thị dòng điện, không voltage | AC-TEL-001/002, AC-SNAP-001 |
| Telemetry Tesla | 0/200/123.456789 hợp lệ, giữ độ chính xác; âm/>200 bị coi không hợp lệ, không clamp; không K | AC-TEL-003 |
| UI phản hồi | API được nhận không tự đổi thông số; chỉ telemetry cập nhật, chưa mẫu đầu hiển thị — disabled | BR-012, AC-DEV-002, AC-TEL-001 |
| Kit idle/nghỉ | Không heartbeat/status/LWT >30s không false offline/Slack; admin ready được xét cấp dù chưa có tín hiệu, UI không giả online | AC-KIT-005 |
| Lượt mới | Last_seen cũ không làm lỗi ngay; mốc giám sát mới, không dùng mẫu cũ của lượt trước | BR-006, AC-KIT-005 |
| Kit reconnect | Một hạn tổng 30s, UI khóa/giữ số cũ, video còn chạy vẫn hiện; trước hạn resume, sau hạn KIT_FAILED/ready thủ công | AC-CONN-002, FR-CONN-005/006 |
| Tie deadline | Hạn kit trùng hạn lượt/ca → TIME_EXPIRED/Đã làm/không redo; callback muộn không đổi nguyên nhân | AC-CONN-003 |
| Web/video lỗi | Web tối đa120s không gia hạn; camera-only không dừng MQTT/control hoặc tạo kit failed | AC-CONN-001, AC-VIDEO-001 |
| Nghỉ kit | R giây từ ended_at cho cả lỗi/thu hồi; R=0 vẫn guard; ready/restart/end lặp không cấp sớm/đổi mốc | AC-KIT-006 |
| Shutdown | Admin gọi được ở bảy trạng thái, topic riêng/tắt toàn bộ; DB inactive, owner ADMIN_TERMINATED/free/redo, không cấp tiếp, giữ lịch | AC-KIT-003/004 |
| Shutdown lỗi/reboot | Không báo đã tắt chỉ vì publish; partial failure báo đúng, không reset/shutdown cũ sau tắt/bật, không Slack lỗi giả | FR-KIT-008, NFR-INT-002 |
| Chống lệnh cũ | End/callback/message lượt A không tác động kit đã sang lượt B; xử lý trùng/trễ theo contract được chốt | AC-ATT-004, NFR-REL-001/002 |
| Báo lỗi/Slack | Lượt hiện tại tiếp tục nhưng chặn cấp tiếp; thông báo dedupe, không thành notification history | AC-ALERT-001/002 |
| Bài nộp/điểm, không Snapshot riêng | Form Phần 2/3 dùng số nhập đã chốt, cấm save/clear sau end; điểm theo lượt không lộ cho SV/không đi MQTT. Telemetry không tự thành bài nộp | AC-SUB-001…006, AC-GRADE-001…003 |
| Tải/phục hồi | 10 kit ×2Hz đúng owner, mục tiêu ≤1s; restart không mất deadline/cooldown hoặc tự replay lệnh | AC-NFR-001/002, NFR-INT-002 |

Nghiệm thu LED Tesla cắt số thừa/không làm tròn, 123.456789→123.4 nhưng MQTT/server/Socket.IO vẫn giữ số gốc (TBD-015 đã đóng). Reset/calib vật lý thuộc kit, nghiệm thu hệ thống ở topic/action/auth/guard và cập nhật telemetry, không tự đặc tả thuật toán vật lý. Ca còn phụ thuộc wire/delivery/hardware khác phải thống nhất contract, không giả telemetry để chứng minh kit thật hoạt động.

## 12. Các điểm còn phải chốt trước triển khai

| SRS / plan | Nội dung còn mở | Nội dung không mở lại |
| --- | --- | --- |
| TBD-002 / G04 | Field telemetry/seq/freshness và kiểm chứng tín hiệu | V08 đã chốt attempt_id echo/heartbeat5s chỉ in-use; idle không heartbeat/status/LWT hoặc chứng minh connected, cấp/start mới bắt đầu tổng30s. Phần mềm kit riêng xử lý MQTT, Python chỉ RTC. Telemetry2Hz/mA/mT/magnetic_field0–200/không K giữ nguyên |
| TBD-002 / G04 | Receiver/session/queue; lỗi đo so với tín hiệu sống; publish/DB partial failure; tràn >4 chữ số phần nguyên LED mA | §16.18 chốt topic/field power-voltage-scale-ruler-calib, attempt_id/command_seq/QoS1/retain=false/không replay. Start-stop chỉ action/attempt_id; shutdown rỗng0byte. Thiết kế V05 session/expiry/interlock tại§6.6 còn kiểm chứng receiver thật. Mốc U gần nhất/tie thấp, LED mA/Tesla cắt số/không làm tròn và thước gom150ms/actual telemetry đã chốt; không hỏi lại |
| TBD-003 / G05 | **Đã đóng — kit chịu trách nhiệm xử lý vật lý reset/calib**. G05 còn nghiệm thu camera/phần mềm kit riêng, không viết MQTT/driver/shutdown OS trong Python | Hệ thống gửi reset đúng topic/action start-stop-calib, auth/guard, chờ telemetry; không implement giá trị đích/thuật toán, không giả calib=0 hoặc đổi timer |
| TBD-008 / G03 | **Đã đóng** | Kit unique≤30, ASCII chữ/số/“-”/“_”, giữ-phân biệt hoa thường, được sửa khi không owner/giữ ID-lịch sử-lịch/về not-ready. Email≤254/loại≤100/mô tả≤1000 giữ nguyên; account code bất biến là quy tắc riêng |
| TBD-010 / G07 | **Đã đóng ưu tiên outage đồng thời** | Web120s=kit30s trước hạn ca/lượt → KIT_FAILED/redo; trùng hạn ca/lượt kể cả ba mốc → TIME_EXPIRED/không redo; các mốc khác theo hạn sớm nhất/không callback order |
| TBD-011 / G08 | V05 chọn marker Redis hiện hành/incident, retry chắc chắn chưa gửi hoặc429; không retry timeout/5xx/crash đang gửi. Còn kiểm chứng Redis/webhook/deployment; không hứa delivery tuyệt đối | Webhook env, event đúng phạm vi, không spam/không notification history |
| TBD-012 / G10 | **Đã đóng nghiệp vụ và topic/payload**; còn nghiệm thu delivery/receiver thật | `lab/kit/{kit_code}/cmd/shutdown` payload rỗng0byte. Toàn kit→inactive, owner end ngay/redo/free nhưng không cấp tiếp, giữ lịch, không tự ready khi bật lại. Python không thực thi shutdown |
| TBD-013 / G12 | **Đã đóng** | Nghỉ giây nguyên≥0/default 0/không max nghiệp vụ; khi kết thúc lượt dùng R mới nhất, lưu cooldown từ ended_at. Sửa lúc đang nghỉ giữ mốc cũ/lần nghỉ sau dùng R mới; cả lỗi/thu hồi, không ack/ready/restart bypass hoặc mở rộng reservation |
| TBD-015 / G04 | **Đã đóng — cắt số thừa, không làm tròn** | LED Tesla cắt phần thập phân dư vừa bốn chữ số, 123.456789→123.4, giữ magnetic_field gốc/mT. Không cắt input I/B₀; chỉ overflow phần nguyên LED mA còn TBD-002 |
| TBD-016 / G13 | **Đã đóng UI interview**; số0 dư/concurrency-race còn TBD-014 | Form phải/25vh, không Back/Replay, spotlight tự căn zoom/pan, không MQTT/reset. Các quyết định thước gom150ms/popup Phần2 trước toàn bài/tour Phần3 một lần mỗi tải trang giữ nguyên. Chấm mới/sửa chỉ sau end/lớp INPROGRESS, không thêm topic cho report/điểm |

Các TBD còn lại của API/Socket.IO/WebRTC hoặc bài nộp/điểm xem SRS §15; guide này không đóng thay. Khi contract được thống nhất, cập nhật đồng thời guide, schema/DTO, API, firmware/FE Streamer và fixture để tránh bốn phía dùng tên/đơn vị khác nhau.

## 13. Chuyển từ guide cũ và lịch sử thay đổi

### 13.1. Các thay đổi cần áp dụng khi tích hợp

- Reset payload rỗng → JSON `action: start | stop | calib`; phải cập nhật cả publisher và consumer, không âm thầm hỗ trợ payload rỗng như contract mới.
- Gỡ khóa K khỏi command, UI và telemetry bắt buộc.
- Bổ sung dòng điện **mA**, LED nguồn hiển thị dòng điện; không dùng điện áp thay thế hoặc đổi sang A.
- `magnetic_field` đúng tên wire, 0–200 mT/không âm/giữ độ chính xác; LED Tesla cắt số thừa/không làm tròn, TBD-015 đã đóng.
- Heartbeat5s/watchdog chỉ in-use, echo attempt_id đúng lượt; idle không yêu cầu tín hiệu/status/LWT, admin ready được xét cấp theo các điều kiện khác.
- Power-voltage-scale-ruler-calib dùng payload đã duyệt tại §6, thêm attempt_id/command_seq tăng dần/QoS1/retain=false/không replay. Không thêm command_seq vào start-stop hoặc payload vào shutdown.
- Tách free ownership khỏi available bởi thời gian nghỉ sau mọi kết thúc.
- Thêm shutdown riêng toàn bộ kit/DB inactive, xử lý owner và lệnh/callback muộn; không thay bằng OFF AC/RESET.
- Không Snapshot riêng; form/dataset thay vai trò ghi số liệu, bài nộp/điểm đi API và đúng scope, không topic MQTT học thuật.
- Bỏ tham chiếu số dòng SRS cũ; dùng ID FR/BR/NFR/AC ổn định và phiên bản nguồn.

### 13.2. Lịch sử

| Ngày | Phiên bản | Ghi chú |
| --- | --- | --- |
| 2026-10-08 | 1.0–1.2 | Các bản guide theo SRS cũ, đã được thay bằng nội dung hiện hành; không dùng lịch sử để suy ra contract đang có hiệu lực |
| 2026-10-11 | 2.0 | Gen lại từ SRS v1.6: telemetry dòng điện mA/độ chính xác từ trường, bỏ K, reset start/stop/calib, heartbeat chỉ in-use, nghỉ kit, shutdown, chống lệnh cũ; phân biệt snapshot/submit/điểm chung theo lượt và đánh dấu rõ draft/TBD |
| 2026-10-11 | 2.1 | Đồng bộ SRS v1.7/guide UI: chốt topic power/action on-off và voltage; núm nguồn theo mốc gần nhất từ điện áp telemetry, không đổi số gốc; range/thước/slider/nút1 mm/zoom-pan và chỉ UI nhập tạm TBD |
| 2026-10-11 | 2.2 | Đồng bộ SRS v1.8: form nhập tay vị trí nguyên cm/B₀ mT thay form cũ; tour/3V chờ telemetry/grid/chart phần 2 đã chốt một phần. Telemetry/label thước mm và dòng điện mA không đổi; không topic/history telemetry mới hoặc change event giả |
| 2026-10-11 | 2.3 | Đồng bộ SRS v1.9: cùng vị trí ghi đè/không khớp telemetry/clear grid-DB chỉ tab hiện hành, hai tab/modal chart/GV hiện hành; toolbar fullscreen thật/calib/đổi layout. B₀ nhập 9999 được phép không đổi dải telemetry; calib vẫn reset action calib |
| 2026-10-12 | 2.4 | Đồng bộ SRS v1.10: fullscreen toàn khu vực thực hành theo layout; lưu/ghi đè/clear chỉ trong lượt, clear có xác nhận; sau end SV đọc lại grid/chart, không điểm hoặc quyền kit. Không đổi topic/payload MQTT, không phát MQTT từ xem/xóa kết quả/fullscreen/layout |
| 2026-10-12 | 2.5 | Đồng bộ SRS v1.11 phần3/gate/hai popup/chart I nhập tay–B₀ qua gốc; form I A không đổi wire/LED/snapshot mA, thước15cm vẫn telemetry150mm/không khóa. Popup không tự end/stop/nhả kit; không topic mới hoặc I₀ đổi dữ liệu kit. Nội dung qua gốc/nguồn mốc và các gate lịch sử đã được đính chính tiếp tại2.6 |
| 2026-10-12 | 2.6 | Đồng bộ SRS v1.12: mốc U gần nhất từ telemetry/server xác định, I/B₀ nhập tay cả U0; đính chính2.5 không ép đường qua gốc/thêm0 giả. Đóng popup phần2 tự sang phần3; clear phần2 giữ phần3/khóa nhập-lưu đến đủ31 lại. Chỉ kiểu vẽ/thứ tự popup/skip-end và tương tác chưa rõ còn mở, tie TBD-002. Thêm toolbar Rời khỏi bài thực hành/confirm, bộ đếm còn lại/popup hết giờ một action; end/stop/free/nghỉ do server xử lý ngay, không đợi bấm/không release lại. Không đổi MQTT/topic/payload/đơn vị hoặc lifecycle kit |
| 2026-10-12 | 2.7 | Đồng bộ SRS v1.13, thay quyết định chart ở 2.6 bằng hồi quy Phần 3 ép qua (0,0), không record giả. Popup thêm ô thiếu/không ghi đè, đóng chart từ popup tự sang Phần 3, skip tour, grid click, input số/dấu chấm/tie thấp, đọc lại không tour-popup. Bỏ Snapshot, thêm Báo kit lỗi text trên toolbar, LED mA cắt bốn chữ số; Tesla vẫn TBD-015. Không thay topic/payload/units/lifecycle |
| 2026-10-12 | 2.8 | Đồng bộ SRS v1.14: duyệt UI-Q-045 thước cộng dồn/gom click 150 ms/nhãn mục tiêu riêng/hủy batch khi reconnect-end; UI-Q-049 popup Phần 2 trước, đóng popup/chart rồi sang Phần 3-hiện popup toàn bài; UI-Q-050 tour Phần 3 một lần mỗi lần tải trang/đổi tab không lặp/refresh chạy lại/luôn Skip. Không đổi topic/payload/units hoặc đóng delivery/fencing/media/chấm điểm còn mở |
| 2026-10-12 | 2.9 | Đồng bộ SRS v1.15: đóng TBD-015 cắt số thừa LED Tesla/không làm tròn; TBD-003 kit chịu trách nhiệm vật lý start-stop-calib, hệ thống gửi MQTT/chờ telemetry; TBD-013 nghỉ nguyên/default 0/không max, R mới lúc end/giữ cooldown hiện tại nếu sửa khi nghỉ. Mốc nearest/tie thấp đã chốt, TBD-002 chỉ wire/delivery/media còn thiếu; không thay topic/payload hoặc thuật toán kit |
| 2026-10-12 | 2.10 | Đồng bộ SRS v1.16: kit_code≤30/loại lỗi≤100/mô tả≤1000; đóng tie web-kit KIT_FAILED, giữ TIME_EXPIRED khi trùng hạn lượt/ca. UI form phải25vh/không Back-Replay/spotlight tự căn, input đủ hai phía/chấm sau end và chỉ INPROGRESS/CSV có điểm không thay MQTT/topic/payload hoặc thuật toán kit |
| 2026-10-12 | 2.11 | SRS v1.17: kit_code regex/case/editable; idle không heartbeat/status/LWT/chứng minh connected trước cấp; start đếm tổng30s từ cấp. Shutdown topic đã chốt/payload rỗng0byte, phần mềm kit riêng nhận MQTT, Python chỉ mẫu RTC. V05 ghi rõ session/expiry/interlock/lỗi publish-DB/Slack, chưa nghiệm thu phần cứng |
| 2026-10-14 | 2.12 | SRS v1.19: ghi nhận xác nhận V08 attempt_id/heartbeat5s/WebRTC trực tiếp, schema và command triển khai local V09–V11; tại thời điểm bàn giao contract command mới còn chờ duyệt, sau đó được thay thế bằng2.13 |
| 2026-10-14 | 2.13 | SRS v1.20 §16.18: chủ sản phẩm đồng ý contract command power-voltage-scale-ruler-calib, attempt_id/command_seq tăng dần/QoS1/retain=false/không tự replay. Start-stop/shutdown giữ contract riêng; code local đã khớp, chưa nghiệm thu firmware/kit/camera/Internet |
| 2026-10-15 | 2.14 | SRS v1.21 §16.19: thêm Python simulator DEMO opt-in, camera máy tính/một webcam chia sẻ hai luồng, nhập kit_code, nhận command/phát tín hiệu theo contract active. Không MQTT topic mới, không thay kit vật lý hoặc OS shutdown; heartbeat/telemetry tổng hợp chỉ khi start |
