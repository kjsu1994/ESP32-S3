#ifndef ESP32S3_CONFIG_EXAMPLE_H
#define ESP32S3_CONFIG_EXAMPLE_H

// Copy this file to config.local.h for a new local setup.
// Do not commit config.local.h; it contains network credentials and API keys.
//
// Board pin maps live in include/boards/.
// Select camera support with the BOARD_HAS_CAMERA build flag.
// Common non-secret defaults live in config.defaults.h.

// Wi-Fi configuration
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// OTA configuration
#define OTA_HOSTNAME "esp32s3"
#define OTA_PASSWORD "CHANGE_ME"

// Home_IOT server configuration
#define HOME_IOT_API_SCHEME "http"
#define HOME_IOT_SERVER_HOST "192.168.0.10"
#define HOME_IOT_SERVER_PORT 8080
#define HOME_IOT_API_BASE_PATH "/api"
// Bootstrap key for first registration. The server returns and stores a random per-device key after registration.
#define HOME_IOT_DEVICE_API_KEY ""
#define HOME_IOT_OWNER_MEMBER_ID 1

// MQTT broker configuration
#define MQTT_BROKER_HOST "192.168.0.10"
#define MQTT_BROKER_PORT 1883

// Device metadata. The web dashboard can override display name and room.
#define DEVICE_NAME "ESP32-S3 Board"
#define DEVICE_TYPE "ESP32_S3"
#define FW_VERSION "0.1.0"

// Non-secret defaults are provided by config.defaults.h and include/boards/.
// Keep config.local.h focused on local credentials and per-install overrides.
//
// Optional per-install camera orientation overrides:
// #define CAMERA_VERTICAL_FLIP true
// #define CAMERA_HORIZONTAL_MIRROR true

#endif
