# ESP32-S3 Camera Stream Recovery Notes

Last updated: 2026-05-22

## Purpose

This note records the current camera streaming state, what was tested, what actually fixed capture, and the next safe direction. Read this before changing camera capture, WebSocket upload, framebuffer, or control behavior.

## Current Goal

- Use the ESP32-S3 camera board as a CCTV-like live camera.
- Prefer low latency over archival quality.
- Keep external viewing through the Home_IOT backend, not direct ESP32 access.
- Keep manual recording support, but do not prioritize recording over live stream stability.

## Current Working Baseline

Selected live-stream profile in `include/config.local.h`:

```cpp
#define CAMERA_UPLOAD_INTERVAL_MS 100
#define CAMERA_FRAME_SIZE FRAMESIZE_QVGA
#define CAMERA_JPEG_QUALITY 20
#define CAMERA_XCLK_FREQ_HZ 14000000
```

The camera driver is currently stable only after changing framebuffer allocation away from PSRAM:

```cpp
config.fb_location = CAMERA_FB_IN_DRAM;
config.fb_count = 1;
config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
```

The previous recovery baseline remains useful when isolating camera driver failures:

```text
[CAMERA] Self-test ok label=boot size=640x480 bytes=13336 elapsed_ms=292 format=4 heap=237464
```

The current selected live profile was measured through the backend/viewer path after USB was disconnected:

```text
viewer_result frames=265 fps=4.07 avg_bytes=5327 total_bytes=1411730 timeouts=0
camera ingest deviceId=esp32s3-2475F6A172E0 fps=4.00 frameBytes=5316 ageMs=0
ping: 0% loss, min 3 ms, max 36 ms, average 6 ms
```

The backend currently overrides the firmware default interval through camera control. The selected backend live interval is `100 ms`:

```text
LIVE_UPLOAD_INTERVAL_MS = 100
```

## What Was Broken

The black screen was not a frontend rendering problem and was not an MJPEG token problem.

The camera was failing before Wi-Fi and before backend/WebSocket involvement:

```text
[CAMERA] Self-test failed label=boot elapsed_ms=4115 heap=313888 psram=yes
```

That means `esp_camera_fb_get()` could not obtain a frame from the camera driver immediately after `esp_camera_init()`.

## Important Findings

- As of the last test on 2026-05-22, `CAMERA_XCLK_FREQ_HZ 16000000` reproduced a hard capture failure on this board state:

```text
[CAMERA] Self-test failed label=boot elapsed_ms=4114 heap=253144 psram=yes
[CAMERA] Recovery start reason=boot-retry failures=0 heap=253144
[CAMERA] Ready
[CAMERA] Self-test failed label=boot-retry elapsed_ms=8000 heap=252940 psram=yes
[CAMERA] Recovery self-test failed reason=boot-retry
[CAMERA] Frame capture failed count=1
```

- Returning only XCLK to `12000000` restored boot self-test and WebSocket uploads during recovery.
- Lowering to QVGA alone did not fix the failure while the old framebuffer setup was still problematic.
- Changing `fb_count` and `grab_mode` alone was not enough until the framebuffer location was changed to DRAM.
- The recovery path works at VGA, 12 MHz, JPEG quality 28.
- The selected live path works better at QVGA, JPEG quality 20, 14 MHz.
- With QVGA at 14 MHz, lowering JPEG quality from `24` to `32` did not meaningfully improve FPS, and raising quality from `24` to `20` kept roughly the same FPS, so `20` is preferred for image quality.
- Lowering backend live interval from `100 ms` to `50 ms` made streaming worse: viewer FPS dropped around `0.26-0.34`, backend ingest stayed around `0.3 fps`, and ping loss returned. Keep `100 ms`.
- The bottleneck moves by profile: VGA was upload/network bound; QVGA/14 MHz is closer to camera frame cadence plus the backend live interval.
- Ping to the board can spike heavily while streaming, which supports the network/upload bottleneck diagnosis.
- Ping during the last successful 12 MHz run had `0%` packet loss but large jitter: min `10 ms`, max `785 ms`, average `161 ms`.

## 2026-05-22 Tuning Results

| Candidate | Result | Decision |
| --- | --- | --- |
| VGA, q28, 12 MHz, backend 100 ms | Frame size roughly 9-13 KB. Upload often ranged from hundreds of ms to seconds under weak Wi-Fi. | Keep only as recovery/reference baseline. |
| VGA, q28, 14 MHz | Boot self-test could pass, but earlier live tests were worse than QVGA and sensitive to network state. | Not selected. |
| QVGA, q24, 12 MHz | Frame size roughly 3.7-5.1 KB. Best idle upload sample was 115 ms, but live FPS stayed around 3.4 when stable. | Good fallback. |
| QVGA, q32, 12 MHz | Similar FPS to q24, lower visual quality. | Not selected. |
| QVGA, q32, 14 MHz, backend 100 ms | Viewer result: 190 frames, 3.45 fps, average 4504 bytes. Later clean run: 244 frames, 4.05 fps, average 4414 bytes. | Good performance, lower image quality than q24. |
| QVGA, q24, 14 MHz, backend 100 ms | Viewer result: 258 frames, 3.97 fps, average 4336 bytes; backend ingest 4.0-4.4 fps; ping 0% loss, avg 13 ms. | Good fallback. |
| QVGA, q20, 14 MHz, backend 100 ms | Viewer result: 265 frames, 4.07 fps, average 5327 bytes; backend ingest 4.0-4.1 fps; ping 0% loss, avg 6 ms. | Selected current profile. |
| QVGA, q24, 14 MHz, backend 50 ms | Control returned LIVE/50, but ingest stayed around 0.3 fps and ping degraded to 16% loss. | Rejected. |

## Backend Changes Added In This Iteration

The backend now has a device-facing control endpoint:

```text
GET /api/devices/{deviceId}/camera/control
```

The intent:

- Viewer absent: return `IDLE`, slow upload interval.
- Viewer present or recording active: return `LIVE`, faster upload interval.

The firmware polls this endpoint periodically and applies only upload interval / streaming mode. Do not add runtime frame-size changes until the camera path is stable; sensor frame-size changes can destabilize testing and make failures harder to isolate.

## Firmware Changes Added In This Iteration

- Added camera self-test immediately after camera init and before Wi-Fi.
- Added capture recovery:
  - boot self-test failure triggers one camera deinit/reinit attempt
  - 5 consecutive runtime capture failures trigger camera recovery
  - recovery has a 10 second cooldown
- Added camera diagnostics logs:
  - `capture_ms`
  - `upload_ms`
  - `total_ms`
  - frame dimensions
  - frame bytes
  - heap
  - upload failure count
- Avoided marking `cameraReady=false` after a single `esp_camera_fb_get()` failure.
- Disabled frequent HTTP fallback by default. WebSocket failure should not immediately trigger expensive HTTP POST loops during live streaming.
- Added server control polling for upload interval.
- Tested scheduling the next successful frame from capture start time instead of upload finish time; reverted because it saturated Wi-Fi/WebSocket upload and caused multi-second `upload_ms` spikes.
- Control polling now runs immediately after registration instead of waiting for the first 5 second poll window, so a no-viewer boot should enter `IDLE` before fast local uploads begin.
- Firmware now starts with the idle upload interval before the first successful control poll. This avoids saturating Wi-Fi with the local live interval when the backend is still starting or the first control request fails.
- Firmware now blocks camera frame upload until at least one control response is received. This prevents slow WebSocket frame writes from starving the control request path during backend/network recovery.
- Added retry spacing for state, telemetry, health, camera status, and camera control requests. Failed HTTP requests should no longer retry every loop and compete with camera traffic.
- Increased device WebSocket handshake/header wait to tolerate LAN jitter. Several failed attempts showed the backend accepting the socket but the firmware reading an empty status line and closing it first.

## What Not To Do Next

- Do not switch back to `XCLK 16 MHz` as the working baseline. It reproduced a boot self-test failure in the latest test. If trying it again, treat it as a single isolated experiment.
- Do not reintroduce PSRAM framebuffers casually. If testing PSRAM again, do it as a single explicit experiment and verify the boot self-test first.
- Do not combine multiple camera changes in one flash. Change only one variable at a time.
- Do not add FreeRTOS task splitting yet. The current bottleneck is upload time; task splitting before the baseline is stable can hide the real failure.
- Do not use runtime frame-size/quality switching yet. Keep camera sensor config stable until ingest is reliable.
- Do not treat a black screen as a frontend issue until firmware logs show successful `Frame ok=true` and backend logs show `camera ingest`.

## Safe Debug Order

1. Check boot self-test.
   - If self-test fails, focus only on camera init/framebuffer/sensor/pin/power.
   - Do not debug backend or UI yet.
2. Check `Frame ok=true` logs.
   - If self-test passes but frame upload fails, inspect WebSocket and network path.
3. Check backend `camera ingest`.
   - If firmware says upload ok but backend has no ingest, inspect WebSocket handler/security/proxy.
4. Check frontend viewer.
   - Only after backend ingest is present.
5. Tune performance.
   - Start with upload/network measurements, not camera capture settings.

## Next Recommended Direction

Short term:

- Keep QVGA, JPEG quality 20, 14 MHz for live use.
- Keep backend `LIVE_UPLOAD_INTERVAL_MS` at `100 ms`; do not use `50 ms` unless doing an isolated saturation test.
- Use OTA only for future firmware uploads while evaluating Wi-Fi load.
- Keep DRAM framebuffer and `fb_count=1`.
- Keep the capture recovery code unless it causes a new failure. It gives clear logs and avoids permanent black-screen states after transient camera driver failures.
- Measure actual `upload_ms` under three conditions:
  - no browser viewer
  - one browser viewer on local LAN
  - one viewer through public route
- Verify whether control endpoint changes the interval between `IDLE` and `LIVE` as expected.
- Avoid reducing the post-upload delay below `100 ms` while Wi-Fi/upload stability is still sensitive.

Medium term:

- Reduce WebSocket upload cost.
- Consider sending fewer frames when upload time exceeds target interval.
- If trying for higher FPS, first solve upload time. `capture_ms` is already low.
- Inspect Wi-Fi conditions and server-side WebSocket write/read behavior because upload time is the dominant delay.

Later:

- Only after stable live streaming, revisit task splitting or latest-frame queue.
- Only after stable live streaming, revisit SVGA/HD experiments.

## Resume Checklist For Next Session

1. Start from the current baseline:
   - `CAMERA_XCLK_FREQ_HZ 14000000`
   - `CAMERA_FRAME_SIZE FRAMESIZE_QVGA`
   - `CAMERA_JPEG_QUALITY 20`
   - backend `LIVE_UPLOAD_INTERVAL_MS 100`
   - `CAMERA_FB_IN_DRAM`
   - `fb_count=1`
   - `CAMERA_GRAB_WHEN_EMPTY`
2. Upload firmware through OTA only:
   - `pio run -c platformio.local.ini -e esp32s3-cam-livingroom-ota -t upload`
3. Open the web page so backend control returns `LIVE`.
4. Capture serial logs if USB is intentionally reconnected; otherwise use backend ingest logs and viewer FPS. Check:
   - `Self-test ok`
   - `Frame ok=true`
   - `capture_ms`
   - `upload_ms`
   - `interval_ms`
5. Check backend logs for `camera ingest` fps and `frameBytes`.
6. Run ping to `192.168.123.120` while streaming to measure jitter.
7. If self-test fails, do not touch backend/frontend. Stay in camera init/XCLK/framebuffer/power debugging.
8. If self-test passes and upload is slow, focus on WebSocket/Wi-Fi/backend ingest path.
