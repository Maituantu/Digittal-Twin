# Video congestion update

- Read already-queued packets directly, avoiding an extra asyncio task for each buffered frame. Empty queues still use the existing stale timeout.
- Yield the event loop after each access unit so RTP senders can run when one pipe read contains multiple frames. No FPS delay is added; dependent P frames are not dropped arbitrarily.
- `capture.queue_frames` sets the per-subscriber queue capacity. Default: 2 for compatibility. Valid range: integer 1..30. The experimental 30 fps profile uses 4 (about 133 ms of video, not an end-to-end latency bound).
- The 30 fps profile uses GOP 15, targeting about 0.5 seconds between recovery opportunities IF the driver emits IDRs at this interval. Verify the actual bitstream on Pi. Shorter GOP can reduce quality at the same bitrate.
- Capture metrics include `fps_recent` and `mbps_recent` over the last 5 seconds (since source creation during the first 5 seconds). These measure encoded access units received, not browser decoded FPS.
- Source `queue_overflows` and `dropped_frames` remain cumulative across closed tracks. Drops are counted per subscriber, not as unique camera frames. `tracks` includes queue occupancy/capacity, drops, overflow count, waiting state and IDR wait duration for active tracks.

## Pi verification

1. Check power/throttling and supported camera modes first.
2. Compare one camera then two, keeping the scene and viewer count equal. Use scripts/monitor.py as documented in docs/benchmark.md.
3. Compare fps_recent, browser decoded FPS and queue overflows. Change queue capacity, GOP and bitrate in separate trials.
4. Do not open a benchmark on a camera that the publisher is already using.

## Remaining limits

Raw Annex-B over the pipe does not preserve capture timestamps; PTS still uses the frame counter and configured FPS. WebRTC PLI/bitrate feedback is not connected to the external FFmpeg encoder. These changes do not establish 30 fps hardware throughput. Existing handover archives have not been rebuilt.

## Local verification

Windows / Python 3.10: 10 selected tests passed, including actual subprocess pipe bursts, queue overflow/IDR recovery, queue validation, rolling metrics and Annex-B decode. Source/tests passed compileall. Full Linux/Python 3.11+ lifecycle, WebRTC and release tests were not run in this environment. No Pi benchmark was performed.
