# ESP32-S3 to CTFMON Integration Guide

Last updated: 2026-05-17

## Purpose
- This document defines how the ESP32-S3 project should integrate with the existing CTFMON web stack.
- Source systems reviewed:
  - Backend: `C:\Users\honeybadger\Desktop\ctfmon_project\CommunityServer\server`
  - Frontend: `C:\Users\honeybadger\Desktop\ctfmon_project\client\api-client-ui`
- This guide is the integration source of truth for ESP32 work that touches REST, MQTT, device state reporting, or homepage connectivity.

## Confirmed CTFMON Environment
- Backend stack: Spring Boot
- Frontend stack: React + Vite
- Frontend API base: `/api`
- Frontend websocket proxy: `/ws`
- OpenAPI source of truth:
  - `http://localhost:8080/swagger-ui/index.html`
  - `http://localhost:8080/v3/api-docs`
- Production domain: `https://ctfmon.shop`

## Integration Model
- Use a mixed `REST + MQTT` model.
- REST responsibilities:
  - device registration
  - device bootstrap/config fetch
  - device metadata update
  - latest device snapshot lookup
  - OTA metadata/history recording
- MQTT responsibilities:
  - realtime commands
  - desired state delivery
  - reported state publish
  - telemetry publish
  - online/offline health and last-seen updates

## Contract Rules
- Do not guess backend contracts from code comments or UI assumptions.
- OpenAPI is the contract source of truth for REST fields, required values, auth, and error shape.
- Default JSON naming is `snake_case`.
- If OpenAPI and local assumptions conflict, stop and align the contract before implementation.
- Frontend uses `/api` and `/ws` proxy paths, but ESP-side docs and firmware should treat backend endpoints and broker endpoints as configurable runtime settings.

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
  - `room_id`
  - `capabilities`
  - `ota_status`
  - `boot_count`
  - `uptime_ms`

## State Model Rules
- Separate `desired state` from `reported state`.
- Do not mark a command as applied when it is only received.
- Report completion only after the hardware state actually changes.
- Design for:
  - Wi-Fi reconnect
  - MQTT reconnect
  - duplicate command delivery
  - offline recovery
  - delayed state sync after reboot

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
  - startup -> fetch config if needed
  - OTA completion/failure -> record summary event
  - UI/admin screen -> read latest stored snapshot from backend
- Keep REST retries bounded and observable. Do not block the firmware forever on backend reachability.

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
- When ESP32 work changes API shape, MQTT payload shape, or device lifecycle semantics, update the matching CTFMON docs as part of the same change set.
- For backend-aligned work, re-check:
  - `CommunityServer\AGENTS.md`
  - `client\api-client-ui\AGENTS.md`
  - OpenAPI docs
- Save Korean or mixed-language docs as UTF-8. Do not rely on PowerShell default encoding.

## Practical First-Step Flows
- Good first integrations for this project:
  - device boot -> REST register -> MQTT connect -> publish `online`
  - homepage/admin view -> show latest device `reported` snapshot from backend
  - command from backend/UI -> MQTT `commands` -> hardware apply -> publish `reported`
  - OTA success/failure -> publish MQTT summary and optionally POST a REST status record
