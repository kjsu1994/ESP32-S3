#ifndef ESP32S3_CONFIG_H
#define ESP32S3_CONFIG_H

// Hardware configuration
#define BOARD_INTERNAL_RGB_PIN 48
#define BOARD_INTERNAL_RGB_ALT_PIN 38
#define BOARD_RGB_COUNT 1

// Wi-Fi configuration
#define WIFI_SSID "U+NetC650"
#define WIFI_PASSWORD "9H421306A#"

// OTA configuration
#define OTA_HOSTNAME "esp32s3"
#define OTA_PASSWORD "esp32s3!"

// Home_IOT server configuration
#define HOME_IOT_SERVER_HOST "192.168.123.118"
#define HOME_IOT_SERVER_PORT 8080
#define HOME_IOT_OWNER_MEMBER_ID 1

// MQTT broker configuration
#define MQTT_BROKER_HOST "192.168.123.118"
#define MQTT_BROKER_PORT 1883

// Device metadata
#define DEVICE_NAME "Living Room Test Board"
#define DEVICE_TYPE "ESP32_S3"
#define FW_VERSION "0.1.0"

#endif
