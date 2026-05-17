#ifndef ESP32S3_CONFIG_H
#define ESP32S3_CONFIG_H

// ==========================================
// 💡 HARDWARE CONFIGURATION (보드 고유 스펙)
// ==========================================
#define BOARD_INTERNAL_RGB_PIN  48  // 내장 RGB LED 핀 번호 고정
#define BOARD_RGB_COUNT          1  // 내장 LED 개수

// Wi-Fi configuration
#define WIFI_SSID "U+NetC650"
#define WIFI_PASSWORD "9H421306A#"

// OTA configuration
#define OTA_HOSTNAME "esp32s3"
#define OTA_PASSWORD "esp32s3!"

// Home_IOT backend configuration
#define HOME_IOT_SERVER_HOST "192.168.123.118"
#define HOME_IOT_SERVER_PORT 8080

// Device metadata
#define DEVICE_NAME "Living Room Test Board"
#define DEVICE_TYPE "ESP32_S3"
#define FW_VERSION "0.1.0"

#endif
