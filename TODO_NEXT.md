# Home_IOT Next TODO

Last updated: 2026-05-19

## Current State
- Home_IOT is running from `C:\Users\honeybadger\Desktop\Home_IOT\IOT compose`.
- Existing ctfmon remains in `C:\compose`.
- ctfmon source and `/api` route are not used by Home_IOT.
- Public routes:
  - `https://ctfmon.shop/iot/` -> Home_IOT frontend through ctfmon nginx proxy
  - `https://ctfmon.shop/iot-api/` -> Home_IOT backend through ctfmon nginx proxy
- ctfmon routes stay unchanged:
  - `/` -> existing ctfmon frontend
  - `/api` -> existing ctfmon backend
  - `/ws` -> existing ctfmon websocket

## Verified
- `https://ctfmon.shop/iot/` returns 200.
- `https://ctfmon.shop/iot/assets/index-IWDV0qL1.js` returns 200 after nginx `^~ /iot/` fix.
- `https://ctfmon.shop/iot-api/devices` returns 401 without login.
- `https://ctfmon.shop/iot-api/auth/me` works with Home_IOT admin Basic auth.
- Existing ESP32 board was OTA-updated to use:
  - `HOME_IOT_API_SCHEME "https"`
  - `HOME_IOT_SERVER_HOST "ctfmon.shop"`
  - `HOME_IOT_SERVER_PORT 443`
  - `HOME_IOT_API_BASE_PATH "/iot-api"`
- Firmware now validates HTTPS with ISRG Root X1 and syncs NTP time before HTTPS calls.

## Important Files
- Home_IOT compose:
  - `C:\Users\honeybadger\Desktop\Home_IOT\IOT compose\docker-compose.yml`
  - `C:\Users\honeybadger\Desktop\Home_IOT\IOT compose\.env`
- ctfmon nginx proxy bridge only:
  - `C:\compose\nginx\nginx.conf`
- Firmware local config:
  - `C:\Users\honeybadger\Desktop\Home_IOT\ESP32S3\include\config.local.h`
- Integration rules:
  - `C:\Users\honeybadger\Desktop\Home_IOT\ESP32S3\HOME_IOT_INTEGRATION.md`

## Tomorrow Priority
1. Open `https://ctfmon.shop/iot/` in the browser and confirm the dashboard loads after `Ctrl + F5`.
2. Confirm the ESP32 board `last_seen_at` updates in the dashboard.
3. Send a simple command from the dashboard, such as `LED_ON` or `NETWORK_STATUS`.
4. Confirm telemetry continues to append after HTTPS migration.
5. If the board does not appear after 1-2 minutes, inspect backend logs and ESP32 serial logs.

## User/Device Ownership Feature
Goal: make each logged-in user see only their own devices.

Recommended model:
- Add user/member table for Home_IOT accounts.
- Add device ownership table or owner field.
- Filter device list by logged-in user.
- Restrict command creation and metadata updates to owned devices.
- Add device registration code flow:
  - logged-in user creates a registration code
  - ESP32 includes that code on first registration
  - backend links the device to that user
  - later requests use the per-device random key

Suggested implementation order:
1. Replace single admin Basic auth with real Home_IOT users.
2. Add device ownership schema.
3. Add registration-code API and UI.
4. Update firmware config to carry a first-registration code when needed.
5. Add tests for cross-user isolation.

## HTTPS Remote OTA Feature
This is useful later when there are many boards or many users.

Recommended flow:
- Admin uploads firmware `.bin` in dashboard.
- Backend stores firmware metadata, version, SHA-256, and download URL.
- Admin sends `OTA_UPDATE` command to one selected device.
- ESP32 downloads firmware over HTTPS.
- ESP32 verifies SHA-256.
- ESP32 writes firmware using `Update`.
- ESP32 reboots and reports OTA success/failure.

Do not start with bulk OTA. First implementation should update one selected device only.

## Security Notes
- Keep `APP_ADMIN_PASSWORD` strong in `IOT compose\.env`.
- Keep `APP_DEVICE_API_KEY` strong in `IOT compose\.env`.
- `APP_DEVICE_API_KEY` is only the bootstrap key. Devices receive their own random key after registration.
- Device-specific keys are stored as hashes in Oracle.
- Firmware stores the issued device key in ESP32 NVS.
- If Cloudflare changes the TLS chain away from ISRG Root X1, update `include/home_iot_tls.h` and rebuild firmware.

## Do Not Touch
- Do not edit ctfmon backend/frontend source for Home_IOT.
- Do not move Home_IOT APIs under public `/api`.
- Do not run Home_IOT services from `C:\compose`.
- `C:\compose` should only contain the nginx proxy bridge for `/iot` and `/iot-api`.
