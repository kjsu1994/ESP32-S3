# ESP32-S3 Camera Stream Recovery Notes

Last updated: 2026-05-23

## Purpose

This note records the current camera streaming state, what was tested, what actually fixed capture, and the next safe direction. Read this before changing camera capture, WebSocket upload, framebuffer, or control behavior.

## Current Goal

- Use the ESP32-S3 camera board as a CCTV-like live camera.
- Prefer low latency over archival quality.
- Keep external viewing through the Home_IOT backend, not direct ESP32 access.
- Keep manual recording support, but do not prioritize recording over live stream stability.

## Current Working Baseline

Current selected live profile:

```cpp
// include/config.defaults.h
#define CAMERA_FRAME_SIZE FRAMESIZE_SVGA
#define CAMERA_JPEG_QUALITY 18
#define CAMERA_FB_COUNT 2

// include/boards/esp32s3_camera_ov5640.h
#define CAMERA_SENSOR_NAME "OV5640"
#define CAMERA_XCLK_FREQ_HZ 14000000

// backend CameraService
LIVE_UPLOAD_INTERVAL_MS = 60
```

The camera driver is currently stable only after changing framebuffer allocation away from PSRAM:

```cpp
config.fb_location = CAMERA_FB_IN_DRAM;
config.fb_count = 2;
config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
```

The previous recovery baseline remains useful when isolating camera driver failures:

```text
[CAMERA] Self-test ok label=boot size=640x480 bytes=13336 elapsed_ms=292 format=4 heap=237464
```

The previous selected live profile was measured through the backend/viewer path after USB was disconnected:

```text
viewer_result frames=265 fps=4.07 avg_bytes=5327 total_bytes=1411730 timeouts=0
camera ingest deviceId=esp32s3-2475F6A172E0 fps=4.00 frameBytes=5316 ageMs=0
ping: 0% loss, min 3 ms, max 36 ms, average 6 ms
```

The backend currently overrides the firmware default interval through camera control. The next target backend live interval is `60 ms`:

```text
LIVE_UPLOAD_INTERVAL_MS = 60
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
- The previous `50 ms` backend test was misleading: firmware rejected `upload_interval_ms < 100`, so the requested value was not actually applied. The firmware now accepts/clamps intervals through `CAMERA_MIN_UPLOAD_INTERVAL_MS` and reports the applied interval in camera status.
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
| OV5640 HVGA, q32, 14 MHz, backend 75 ms, async server ingest | Backend ingest 4.5 fps, frame size about 6.7-7.0 KB. Firmware status: capture 131 ms, upload 8-9 ms, total 139-140 ms, applied interval 75 ms. Server metrics: copy 9 us, queue 121 us, accept 210 us, notify 71 us. Ping 0% loss, avg 5 ms. | Server/client processing is not the current FPS bottleneck. Next target is camera capture wait and scheduler. |
| OV5640 HVGA, q32, 14 MHz, backend 75 ms, fb_count=2, async server ingest | Backend ingest reached 9.1 fps, frame size about 6.7-6.8 KB. Firmware status: capture 24 ms, upload 9 ms, total 33 ms, applied interval 75 ms. Server metrics: copy 13 us, queue 78 us, accept 128 us, notify 58 us. Ping 0% loss, avg 7 ms. | Good performance reference. |
| OV5640 HVGA, q32, 14 MHz, backend 67 ms, fb_count=2, schedule-from-start | Backend ingest stayed around 9.0-9.1 fps. Firmware status: capture 69-91 ms, upload 8-10 ms, total 78-101 ms. Ping 0% loss, avg 11 ms, max 182 ms. | 15 fps not reached; schedule-from-start did not improve this sensor mode. |
| OV5640 VGA, q32, 14 MHz, backend 67 ms, fb_count=2, schedule-from-start | Backend ingest rose from 6.8 to 8.2 fps after settling. Frame size about 11.4-12.2 KB. Firmware status: capture commonly 73-88 ms with spikes to 199-208 ms, upload 15-27 ms. Ping 0% loss, avg 9 ms, max 117 ms. | VGA streams, but not at 15 fps. Not selected. |
| OV5640 HVGA, q20, 14 MHz, backend 75 ms, fb_count=2 | Backend ingest stayed around 8.8-9.1 fps. Frame size about 8.5-8.8 KB. Firmware status: capture 0-19 ms, upload 15-18 ms, total 17-37 ms. Ping 0% loss, avg 9 ms, max 63 ms. | Selected current balanced quality/performance profile. |
| OV5640 VGA, q15, 16 MHz, PSRAM fb_count=2, GRAB_LATEST | Community-style profile initially reported VGA but then fell into repeated capture timeouts (`captureMs` 4067-8000, no frame bytes) and even OTA authentication needed streaming to be disabled temporarily for recovery. | Rejected for this board state. |
| OV5640 VGA, q15, 14 MHz, DRAM fb_count=2, GRAB_WHEN_EMPTY, backend 50 ms | Backend ingest stayed mostly 8.0-9.0 fps after settling. Frame size about 15.1-17.8 KB. Firmware status usually total 26-104 ms, with observed upload spikes up to 1407 ms. Ping over 120 packets: 0% loss, min 2 ms, max 127 ms, avg 11 ms. | Highest FPS candidate, but spikier than 60 ms. |
| OV5640 VGA, q15, 14 MHz, DRAM fb_count=2, GRAB_WHEN_EMPTY, backend 55 ms | Recent settled sample: backend ingest avg 7.43 fps, frame size avg 16.6 KB, upload avg 97 ms/max 219 ms, total avg 119 ms/max 237 ms. Ping over 90 packets: 0% loss, min 2 ms, max 48 ms, avg 8 ms. | Rejected; worse latency than 60 ms without a useful FPS gain. |
| OV5640 VGA, q15, 14 MHz, DRAM fb_count=2, GRAB_WHEN_EMPTY, backend 60 ms | Settled sample: backend ingest avg 7.85 fps, frame size avg 17.7 KB, capture avg 18.6 ms, upload avg 37.4 ms/max 51 ms, total avg 56 ms/max 77 ms. Ping over 90 packets: 0% loss, min 2 ms, max 125 ms, avg 11 ms. | Selected current VGA quality/stability profile. |
| OV5640 VGA, q15, 14 MHz, DRAM fb_count=2, GRAB_WHEN_EMPTY, backend 75 ms | Recent settled sample: backend ingest avg 7.73 fps, frame size avg 16.3 KB, upload avg 45.5 ms/max 83 ms, total avg 49 ms/max 83 ms. Ping over 90 packets: 0% loss, min 2 ms, max 170 ms, avg 8 ms. | Stable, but not better enough than 60 ms. |
| OV5640 SVGA, q18, 14 MHz, DRAM fb_count=2, GRAB_WHEN_EMPTY, backend 75 ms | Confirmed `800x600`. Recent stable samples defended about 7.5-8.1 fps, with frame size often around 20-26 KB depending on scene. Typical status after settling showed capture 0-60 ms, upload 50-128 ms, total 59-161 ms, failures 0/0. Earlier 90-packet ping sample had 0% loss, min 2 ms, max 114 ms, avg 12 ms. | Selected current high-quality profile; SVGA increased resolution without collapsing FPS. |
| OV5640 SVGA, q18, 14 MHz, DRAM fb_count=2, GRAB_WHEN_EMPTY, backend 75 ms, schedule-from-start | FPS did not break past the same ceiling, while network health got worse: ping 1% loss, max 972 ms, avg 48 ms; upload max 1600 ms, total max 1600 ms. | Rejected and rolled back to finish-based scheduling. |

## 2026-05-23 Web Client Rendering Iteration

Firmware camera values were not changed in this iteration. The selected stream profile remained OV5640 `SVGA 800x600`, JPEG quality `18`, XCLK `14 MHz`, DRAM `fb_count=2`, `CAMERA_GRAB_WHEN_EMPTY`, backend live interval `75 ms`.

Changes made on the web path:

- Browser viewer now prefers `createImageBitmap(blob)` plus WebGL2/WebGL rendering.
- Canvas2D remains the fallback path when GPU rendering is unavailable.
- Viewer renders only the latest pending frame; stale pending frames are counted as dropped instead of being rendered late.
- Viewer UI now reports source FPS, render FPS, decode time, draw time, renderer, GPU name when available, dropped frames, and frame size.
- Backend viewer WebSocket now logs send-side metrics every 10 seconds.
- The client has `Natural` and `Sharp` rendering presets. `Sharp` applies mild shader-based sharpening plus small contrast/saturation adjustment.

Observed result after Docker rebuild/restart:

```text
camera viewer websocket sentFrames=77 overwrittenFrames=0 bytes=1706569 avgSendMs=1.08 maxSendMs=2.04 pending=false
camera ingest fps=7.50 frameBytes=22185 listeners=1
camera status size=800x600 captureMs=13 uploadMs=52 totalMs=65 intervalMs=75 frameBytes=22209 failures=0/0
```

Conclusion:

- Server-to-browser WebSocket send was not the bottleneck in this sample.
- The server sent roughly the same cadence it ingested, with no overwritten viewer frames and about `1 ms` average send time.
- Further perceived quality improvements should be evaluated in the browser renderer first.
- Further real FPS improvements still depend mostly on camera/upload cadence, not Java backend frame dispatch.

Rendering v2:

- Added a third browser renderer preset: `Detail`.
- `Detail` increases shader sharpening compared with `Sharp`, adds weak flat-area denoise, and applies slightly stronger contrast/saturation.
- `Sharp` now also applies a very small denoise pass to avoid making JPEG block noise stand out too much.
- WebGL rendering now adjusts sharpen/denoise strength upward slightly when the displayed canvas is larger than the source frame.
- `Natural` remains the no-enhancement comparison mode.

Rendering pipeline follow-up:

- Browser JPEG decode and canvas draw are now separated.
- The viewer starts `createImageBitmap` decode immediately when a WebSocket Blob arrives.
- `requestAnimationFrame` now draws only already-decoded frames, so render FPS should stay closer to source FPS.
- Dropped frames now primarily mean either decode could not keep up with incoming frames or an already decoded frame was replaced before the next paint.

Follow-up change:

- Backend live interval was lowered from `75 ms` to `60 ms`.
- Firmware did not need an OTA change because `CAMERA_MIN_UPLOAD_INTERVAL_MS` is `50 ms` and backend control already drives the live upload interval.
- First post-change sample confirmed `intervalMs=60`: backend ingest stayed around `7.6 fps`, firmware status showed `captureMs=0-23`, `uploadMs=32-46`, `totalMs=32-69`, failures `0/0`, and viewer WebSocket had `sentFrames=76-78`, `overwrittenFrames=0`, `avgSendMs=1.28-1.32`.

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

- Use the OV5640 target profile: SVGA 800x600, JPEG quality 18, 14 MHz, DRAM `fb_count=2`, `CAMERA_GRAB_WHEN_EMPTY`, backend `LIVE_UPLOAD_INTERVAL_MS 60`.
- Use the reported `applied_interval_ms` to verify that backend control values are actually applied.
- Use OTA only for future firmware uploads while evaluating Wi-Fi load.
- Keep DRAM framebuffer and `fb_count=2`.
- Keep the capture recovery code unless it causes a new failure. It gives clear logs and avoids permanent black-screen states after transient camera driver failures.
- Keep `CAMERA_SCHEDULE_FROM_START` disabled. It increased network jitter on SVGA without improving FPS.
- The frontend canvas now draws at display size times device pixel ratio and applies a small contrast/saturation boost, so browser scaling should not hide the extra SVGA detail.
- Measure actual `upload_ms` under three conditions:
  - no browser viewer
  - one browser viewer on local LAN
  - one viewer through public route
- Verify whether control endpoint changes the interval between `IDLE` and `LIVE` as expected.
- If FPS becomes more important than image quality, compare against the previous VGA/q15/60 ms profile before changing transport logic.

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
   - `CAMERA_FRAME_SIZE FRAMESIZE_SVGA`
   - `CAMERA_JPEG_QUALITY 18`
   - `CAMERA_FB_COUNT 2`
   - backend `LIVE_UPLOAD_INTERVAL_MS 60`
   - `CAMERA_FB_IN_DRAM`
   - `CAMERA_GRAB_WHEN_EMPTY`
   - finish-based scheduling (`CAMERA_SCHEDULE_FROM_START false`)
2. Upload firmware through OTA only:
   - `pio run -c platformio.local.ini -e esp32s3-cam-livingroom-ota -t upload`
3. Open the web page so backend control returns `LIVE`.
4. Capture serial logs if USB is intentionally reconnected; otherwise use backend ingest logs and viewer FPS. Check:
   - `Self-test ok`
   - `Frame ok=true`
   - `capture_ms`
   - `upload_ms`
   - `total_ms`
   - `interval_ms`
   - `applied_interval_ms`
5. Check backend logs for `camera ingest` fps and `frameBytes`.
6. Run ping to `192.168.123.120` while streaming to measure jitter.
7. If self-test fails, do not touch backend/frontend. Stay in camera init/XCLK/framebuffer/power debugging.
8. If self-test passes and upload is slow, focus on WebSocket/Wi-Fi/backend ingest path.
