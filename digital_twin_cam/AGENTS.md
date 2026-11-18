# Phạm vi project

- Project độc lập, chỉ camera. Không import source từ repo digital_twin cũ.
- Không sửa web chính thức trên server hoặc đổi dịch vụ Pi để kiểm tra tự động.
- Không lưu khóa publish, mật khẩu SSH hay token trong Git, tài liệu hoặc log.
- Mặc định H.264 phần cứng qua FFmpeg h264_v4l2m2m; không fallback libx264.
- Đọc docs/architecture trước khi đổi topology. Server chính thức chỉ signaling;
  media hiện đi Pi → browser. Không mô tả nó là server relay.
- Phân biệt kết quả test trên PC với benchmark trên Pi; không suy đoán hardware OK.
- Tests chạy: python -m unittest discover -s tests -v.

## Camera deployment, 2026-10-23

- Current local source includes V2 queue/metrics improvements and shared `video` YAML settings.
- Pi active project is V2 at the canonical `/home/thang/digital_twin_cam`, with its own `.venv`; use `.venv/bin/dt-cam --config config/local.yaml publish`. Preserve this path for the MQTT integration contract.
- Pi V1 `/home/thang/digital_twin_cam` was backed up under `~/.local/state/digital_twin_cam/deployments/before-v2-switch-*.tar.gz` before replacement by V2 at the same canonical path. Local V1 backup is `/home/ducvu/.local/state/digital_twin_cam/v1-before-v2-20261023-153123.tar.gz`.
- Old MJPEG `digital-twin-camera.service` belongs to `/opt/digital_twin`, is distinct from V1/V2, and can steal USB camera devices after reboot. It was stopped during migration; no boot disable was applied.
- Shared `video` applies FPS/dimensions/bitrate/GOP to all cameras; per-camera video fields are rejected when `video` exists. Runtime config is 640x480 at 15 FPS, 1 Mbps per camera.
- Do not start a duplicate publisher: check running processes first. The V2 launcher no longer depends on V1's environment.
- Old dist archives have not been rebuilt for this migration.
