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

Local firmware config in `include/config.local.h` is currently intended to stay around:

```cpp
#define CAMERA_UPLOAD_INTERVAL_MS 100
#define CAMERA_FRAME_SIZE FRAMESIZE_VGA
#define CAMERA_JPEG_QUALITY 28
#define CAMERA_XCLK_FREQ_HZ 16000000
```

The camera driver is currently stable only after changing framebuffer allocation away from PSRAM:

```cpp
config.fb_location = CAMERA_FB_IN_DRAM;
config.fb_count = 1;
config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
```

Observed successful boot self-test after that change:

```text
[CAMERA] Self-test ok label=boot size=640x480 bytes=9134 elapsed_ms=219 format=4
```

Observed successful upload after that change:

```text
[CAMERA] Frame ok=true transport=websocket size=640x480 bytes=11166 capture_ms=1 upload_ms=1787 total_ms=1788 interval_ms=100
camera ingest deviceId=esp32s3-2475F6A172E0 ... frameBytes=11166
```

## What Was Broken

The black screen was not a frontend rendering problem and was not an MJPEG token problem.

The camera was failing before Wi-Fi and before backend/WebSocket involvement:

```text
[CAMERA] Self-test failed label=boot elapsed_ms=4115 heap=313888 psram=yes
```

That means `esp_camera_fb_get()` could not obtain a frame from the camera driver immediately after `esp_camera_init()`.

## Important Findings

- `CAMERA_XCLK_FREQ_HZ 16000000` is not proven to be the root cause. The camera worked again at 16 MHz after moving framebuffer allocation to DRAM.
- Lowering to QVGA alone did not fix the failure while the old framebuffer setup was still problematic.
- Changing `fb_count` and `grab_mode` alone was not enough until the framebuffer location was changed to DRAM.
- The capture path can work at VGA, 16 MHz, JPEG quality 28.
- Current capture time is very low, usually around `0-1 ms`.
- Current bottleneck is upload/network, not camera capture. WebSocket upload has been observed around `1700 ms` or worse for a VGA JPEG frame.
- Ping to the board can spike heavily while streaming, which supports the network/upload bottleneck diagnosis.

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

## What Not To Do Next

- Do not assume `XCLK 16 MHz` is the cause without testing the boot self-test.
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

- Keep VGA, quality 28, 16 MHz.
- Keep DRAM framebuffer and `fb_count=1`.
- Measure actual `upload_ms` under three conditions:
  - no browser viewer
  - one browser viewer on local LAN
  - one viewer through public route
- Verify whether control endpoint changes the interval between `IDLE` and `LIVE` as expected.

Medium term:

- Reduce WebSocket upload cost.
- Consider sending fewer frames when upload time exceeds target interval.
- If trying for higher FPS, first solve upload time. `capture_ms` is already low.

Later:

- Only after stable live streaming, revisit task splitting or latest-frame queue.
- Only after stable live streaming, revisit SVGA/HD experiments.
