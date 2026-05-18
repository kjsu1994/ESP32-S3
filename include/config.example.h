#ifndef ESP32S3_CONFIG_EXAMPLE_H
#define ESP32S3_CONFIG_EXAMPLE_H

// Hardware configuration
#define BOARD_INTERNAL_RGB_PIN 48
#define BOARD_INTERNAL_RGB_ALT_PIN 38
#define BOARD_RGB_COUNT 1

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
#define HOME_IOT_TLS_CA_CERT ISRG_ROOT_X1_CA_CERT
#define HOME_IOT_TLS_ALLOW_INSECURE false
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

#endif
