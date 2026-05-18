# ESP32-S3 to Home_IOT Web Integration Guide

Last updated: 2026-05-18

## Purpose
- This document defines how the ESP32-S3 firmware integrates with the Home_IOT web stack.
- Actual implementation workspace:
  - Project root: `C:\Users\honeybadger\Desktop\Home_IOT`
  - Backend: `C:\Users\honeybadger\Desktop\Home_IOT\backend\server`
  - Frontend: `C:\Users\honeybadger\Desktop\Home_IOT\client\api-client-ui`
  - Firmware: `C:\Users\honeybadger\Desktop\Home_IOT\ESP32S3`
- `C:\Users\honeybadger\Desktop\ctfmon_project` is reference-only. Do not edit it for Home_IOT work.

## Reference Boundary
- ctfmon may be inspected only for structure, style, or implementation patterns.
- Do not add Home_IOT backend APIs, frontend pages, routes, docs, or migrations under `ctfmon_project`.
- If a ctfmon document appears to conflict with this file, follow this file for Home_IOT work.

## Confirmed Home_IOT Environment
- Backend stack: Spring Boot
- Frontend stack: React + Vite
- Frontend API base: `/api`
- Backend packages use `com.homeiot`.
- Current backend domains include device, command, telemetry, MQTT publisher, and OTA history.

## Integration Model
- Use a mixed `REST + MQTT` model.
- REST responsibilities:
  - device registration
  - device bootstrap/config fetch
  - device metadata update
  - latest device snapshot lookup
  - queued command polling fallback
  - OTA metadata/history recording
- MQTT responsibilities:
  - realtime commands
  - desired state delivery
  - reported state publish
  - telemetry publish
  - online/offline health and last-seen updates

## Contract Rules
- Do not guess backend contracts from comments or UI assumptions.
- Home_IOT backend DTOs and controllers are the local contract source of truth until OpenAPI is added.
- Default JSON naming is `snake_case`.
- Frontend uses `/api`, but firmware endpoints must remain configurable in `include/config.h` or later external config.
- On a shared ctfmon domain, Home_IOT must not use `/api` publicly. Public Home_IOT API traffic must use `/iot-api`.
- Public `/iot-api/*` is reverse-proxied to Home_IOT backend `/api/*`.
- Public `/iot/*` is reserved for the Home_IOT frontend.
- Existing ctfmon `/`, `/api`, `/ws`, and other production paths must not be changed by Home_IOT deployment.

## Shared Domain Deployment Rules
- Safe public path layout:
  - `https://ctfmon.shop/` -> existing ctfmon frontend
  - `https://ctfmon.shop/api/*` -> existing ctfmon backend
  - `https://ctfmon.shop/iot/*` -> Home_IOT frontend
  - `https://ctfmon.shop/iot-api/*` -> Home_IOT backend `/api/*`
- Home_IOT frontend build variables for shared-domain deployment:
  - `VITE_BASE_PATH=/iot/`
  - `VITE_API_BASE_PATH=/iot-api`
- Firmware deployment settings for shared-domain deployment:
  - `HOME_IOT_API_SCHEME "https"`
  - `HOME_IOT_SERVER_HOST "ctfmon.shop"`
  - `HOME_IOT_SERVER_PORT 443`
  - `HOME_IOT_API_BASE_PATH "/iot-api"`
- Firmware HTTPS validation:
  - Current `ctfmon.shop` chain checked on 2026-05-19: `Let's Encrypt E7 -> ISRG Root X1`.
  - Firmware pins `ISRG Root X1` through `HOME_IOT_TLS_CA_CERT`.
  - `HOME_IOT_TLS_ALLOW_INSECURE` should stay `false` for public deployment.
  - If Cloudflare changes the edge certificate chain to a different root CA, update `include/home_iot_tls.h` and rebuild firmware.
  - HTTPS certificate validation requires time; firmware syncs NTP after Wi-Fi connects.
- Do not deploy Home_IOT under public `/api`; that path belongs to ctfmon.
- Do not edit ctfmon source code for Home_IOT deployment. Use reverse proxy routing only.
- The Home_IOT dashboard and control APIs require the Home_IOT admin account.
- Configure `APP_ADMIN_USERNAME` and a strong `APP_ADMIN_PASSWORD` before exposing `/iot` publicly.

## Device Identity and Payload Baseline
- Every device-facing integration should carry a stable `device_id`.
- Firmware-provided `name` is a fallback name. User-facing device names and room labels are managed by the web server.
- Recommended baseline fields:
  - `device_id`
  - `display_name`
  - `room`
  - `device_type`
  - `fw_version`
  - `ip`
  - `rssi`
  - `online`
  - `last_seen_at`
- Optional fields when relevant:
  - `room`
  - `capabilities`
  - `ota_status`
  - `boot_count`
  - `uptime_ms`

## State Model Rules
- Separate `desired state` from `reported state`.
- Do not mark a command as applied when it is only received.
- Report completion only after the hardware state actually changes.
- Design for Wi-Fi reconnect, MQTT reconnect, duplicate command delivery, offline recovery, and delayed state sync after reboot.

## MQTT Topic Baseline
- Unless a later contract overrides it, use this baseline topic layout:
  - `devices/{device_id}/desired`
  - `devices/{device_id}/commands`
  - `devices/{device_id}/reported`
  - `devices/{device_id}/telemetry`
  - `devices/{device_id}/health`
  - `devices/{device_id}/ota`
- Topic payloads should remain small, explicit, and versionable.
- Include `fw_version` and `device_id` in MQTT payloads that affect operations or diagnosis.

## REST Usage Baseline
- REST should be used for registration/bootstrap style flows, not high-frequency telemetry.
- Suggested flow:
  - first boot or explicit re-provision -> register device
  - command delivery fallback -> device polls latest queued command
  - periodic device diagnostics -> POST telemetry event
  - startup -> fetch config if needed
  - OTA completion/failure -> record summary event
  - UI screen -> read latest stored snapshot from backend
- Keep REST retries bounded and observable. Do not block firmware forever on backend reachability.

## Telemetry Event Baseline
- Firmware sends telemetry to:
  - `POST /api/devices/{device_id}/telemetry`
- Current firmware event types:
  - `boot`: sent after successful registration
  - `wifi_reconnected`: sent after Wi-Fi is restored and the device can reach the backend
  - `mqtt_connected`: sent when MQTT reconnects
  - `mqtt_disconnected`: sent when MQTT drops while HTTP is still reachable
  - `heartbeat`: sent every 60 seconds with the current reported-state snapshot
- Telemetry is append-only operational history. The latest current state still belongs in `/state/report`.

## REST Command Delivery Baseline
- The web UI queues commands in the backend first.
- Firmware polls:
  - `GET /api/devices/{device_id}/commands/pending`
- If a command is returned, firmware reports the command lifecycle:
  - `PATCH /api/devices/{device_id}/commands/{command_id}` with `stage=RECEIVED`
  - after hardware action, `PATCH /api/devices/{device_id}/commands/{command_id}` with `stage=APPLIED` or `FAILED`
- MQTT command delivery can still be used, but command payloads should include `command_id` so the same lifecycle endpoint can be updated.

## Firmware Implementation Rules
- Keep secrets and environment-specific endpoints out of hardcoded source whenever practical.
- `include/config.h` is a small loader only.
- Put real Wi-Fi, OTA, API, and MQTT values in `include/config.local.h`.
- Keep shareable defaults in `include/config.example.h`.
- `include/config.local.h` must stay untracked and must not be committed.
- If firmware grows, split responsibilities out of `src/main.cpp` into modules:
  - network session management
  - MQTT client lifecycle
  - REST client/bootstrap
  - payload serialization
  - hardware state application
- LED status remains a diagnostic surface and should reflect meaningful connectivity or error states.

## Security and Operations
- Do not hardcode production hostnames, tokens, broker credentials, or API secrets into firmware intended for reuse.
- Treat domain, API base URL, broker host, broker port, and auth tokens as environment-specific settings.
- Keep OTA password, Wi-Fi credentials, API tokens, and MQTT credentials separated from public repo sharing whenever possible.
- If a backend-facing feature requires auth or sensitive validation, prefer backend enforcement over client-only logic.
- Public deployments must set `APP_DEVICE_API_KEY` on the backend as the device bootstrap key.
- Firmware uses local `HOME_IOT_DEVICE_API_KEY` only for first registration/bootstrap.
- On successful registration, the backend issues a random device-specific API key and stores only its hash in Oracle.
- Firmware stores the issued device-specific API key in ESP32 NVS and uses it for later device-facing requests.
- If the same board is flashed again and loses NVS, it can re-register with the bootstrap key and receive a rotated device key.
- Device-facing endpoints require `X-Device-Api-Key` when the backend bootstrap key is configured. Normal device endpoints require the issued per-device key after provisioning.
- Dashboard read/control endpoints require Home_IOT admin HTTP Basic authentication.
- The frontend stores the admin Basic auth header in browser localStorage for the current MVP. Use HTTPS in public deployment.

## New Board Setup
- First flash a new board with USB:
  - `pio run -e esp32s3-usb -t upload`
- Reuse the same firmware across boards when they share Wi-Fi and backend settings.
- Device identity is generated from the ESP32 MAC address, so multiple boards register as distinct devices.
- After a board appears on Wi-Fi, create a board-specific OTA env in ignored `platformio.local.ini`.
- Use tracked `platformio.ota.example.ini` as the template for local OTA targets.
- To upload with a local OTA config, run:
  - `pio run -c platformio.local.ini -e esp32s3-livingroom-ota -t upload`
- Do not set `default_envs` to an OTA target. The default should stay `esp32s3-usb` to avoid accidentally uploading to the wrong board.

## Documentation Rules
- When ESP32 work changes API shape, MQTT payload shape, or device lifecycle semantics, update the matching Home_IOT docs as part of the same change set.
- For backend-aligned work, re-check:
  - `C:\Users\honeybadger\Desktop\Home_IOT\backend\AGENTS.md`
  - `C:\Users\honeybadger\Desktop\Home_IOT\client\api-client-ui\AGENTS.md`
  - Home_IOT backend DTOs/controllers
- Save Korean or mixed-language docs as UTF-8. Do not rely on PowerShell default encoding.

## Practical First-Step Flows
- Good first integrations for this project:
  - device boot -> REST register -> MQTT connect -> publish `online`
  - homepage/admin view -> show latest device `reported` snapshot from backend
  - command from backend/UI -> backend queue -> firmware poll or MQTT command -> hardware apply -> update command lifecycle
  - OTA success/failure -> publish MQTT summary and optionally POST a REST status record
