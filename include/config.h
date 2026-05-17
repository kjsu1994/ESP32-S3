//
// Created by honeybadger on 26. 5. 17..
//

#ifndef ESP32S3_CONFIG_H
#define ESP32S3_CONFIG_H

// ==========================================
// 💡 HARDWARE CONFIGURATION (보드 고유 스펙)
// ==========================================
#define BOARD_INTERNAL_RGB_PIN  48  // 내장 RGB LED 핀 번호 고정
#define BOARD_RGB_COUNT          1  // 내장 LED 개수

// ==========================================
// 🌐 NETWORK CONFIGURATION (와이파이 설정)
// ==========================================
// 192.168.123.116
#define WIFI_SSID       "U+NetC650"
#define WIFI_PASSWORD   "9H421306A#"

// 🚀 OTA CONFIGURATION (무선 배포 설정)
#define OTA_HOSTNAME    "esp32s3"       // 앞으로 IP 대신 쓸 이름 (보드 도메인)
#define OTA_PASSWORD    "esp32s3!"      // 무선 코드를 밀어 넣을 때 쓸 인증 암호

#endif //ESP32S3_CONFIG_H
