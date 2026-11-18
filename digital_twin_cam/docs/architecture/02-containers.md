# C4 Level 2 — Containers

```mermaid
flowchart LR
  subgraph PI["Deployment node: Raspberry Pi 3B"]
    UVC["USB UVC device
MJPG 640x480, 15 FPS thử ban đầu"]
    FF["FFmpeg child process / camera
JPEG decode CPU -> YUV420 -> h264_v4l2m2m"]
    APP["Python publisher
PyAV parse + aiortc packetize + aiohttp WS"]
    CFG["Local files
YAML và key tách biệt"]
    UVC -->|V4L2 USB| FF
    FF -->|stdout pipe: H264 Annex-B| APP
    CFG --> APP
  end
  subgraph SERVER["Deployment node: server 10.147.19.155"]
    NG["Nginx
HTTP UI + proxy WebSocket"]
    API["Official API
Auth, kit, attempt"]
    SIG["Official streaming/signaling
Chuyển SDP và ICE config"]
    NG --> API
    NG --> SIG
  end
  B["Browser
RTCPeerConnection + H264 decoder"]
  APP <-->|WS /signaling qua Nginx| NG
  B <-->|HTTP UI/API, signaling có token| NG
  APP -->|ICE + DTLS-SRTP / RTP H264| B
```

| Kênh | Mang gì | Không mang gì |
| --- | --- | --- |
| USB/V4L2 | JPEG của camera | Không phải giao thức Internet |
| FFmpeg stdout pipe | H.264 đã encode trong Pi | Không đi qua PC/HTTP MJPEG |
| WebSocket signaling | publish, offer/answer SDP, peerId, ICE config | Không chở frame video |
| WebRTC media | H264 đóng RTP rồi bảo vệ DTLS-SRTP; thường UDP | Không đi qua server chỉ vì server phục vụ UI |

Server do CNTT quản lý; project không build/chạy lại Node, Nginx hoặc database.
Một camera = một FFmpeg encoder, nhiều track nhận packet đã nén. Hai camera =
hai yêu cầu encode, cùng chia sẻ phần cứng Pi; khả năng đồng thời phải benchmark.
Một port signaling không tương ứng một encoder, một core hoặc một người xem.
