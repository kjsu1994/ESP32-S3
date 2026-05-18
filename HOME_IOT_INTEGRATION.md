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

## Device Identity and Payload Baseline
- Every device-facing integration should carry a stable `device_id`.
- Recommended baseline fields:
  - `device_id`
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
  - startup -> fetch config if needed
  - OTA completion/failure -> record summary event
  - UI screen -> read latest stored snapshot from backend
- Keep REST retries bounded and observable. Do not block firmware forever on backend reachability.

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
- Current `include/config.h` may be used as a temporary location, but long-term API, MQTT, and OTA settings should move to injected or externalized config.
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
