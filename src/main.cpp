#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>        // IP 대신 이름을 쓰게 해주는 mDNS 라이브러리
#include <ArduinoOTA.h>     // OTA 무선 배포 라이브러리
#include <Adafruit_NeoPixel.h>
#include "config.h"

Adafruit_NeoPixel pixel(BOARD_RGB_COUNT, BOARD_INTERNAL_RGB_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
    Serial.begin(115200);
    pixel.begin();
    pixel.setBrightness(40);

    // 시작 상태는 빨간색 (🔴 연결 대기)
    pixel.setPixelColor(0, pixel.Color(255, 0, 0));
    pixel.show();

    // 1. 와이파이 연결 시도
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    // 와이파이 연결 성공 (🟢 초록불)
    pixel.setPixelColor(0, pixel.Color(0, 255, 0));
    pixel.show();

    Serial.println("\n🌐 WiFi Connected!");
    Serial.print("📌 IP Address: ");
    Serial.println(WiFi.localIP()); // ◀ 여기서 찍히는 IP주소를 메모해 두세요!

    // 2. OTA 무선 배포 서버 설정
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    // OTA 진행 상태에 따른 LED 피드백 정의 (이벤트 리스너)
    ArduinoOTA.onStart([]() {
        Serial.println("🚀 OTA Update Started...");
        pixel.setPixelColor(0, pixel.Color(0, 0, 255)); // 다운로드 시작하면 파란불 (🔵)
        pixel.show();
    });

    ArduinoOTA.onEnd([]() {
        Serial.println("\n✅ OTA Update Finished! Rebooting...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        pixel.setPixelColor(0, pixel.Color(255, 0, 0)); // 에러 나면 다시 빨간불
        pixel.show();
    });

    ArduinoOTA.begin(); // OTA 백그라운드 서버 기동
}

void loop() {
    ArduinoOTA.handle(); // ◀ 백엔드의 이벤트 루프처럼 무선 신호가 오는지 매 순간 감시 (필수!)

    // 헬스 체크: 와이파이 연결 유지 중이면 초록불 유지
    if (WiFi.status() == WL_CONNECTED) {
        // 여기에 원래 하고 싶으신 비즈니스 로직을 짜 넣으시면 됩니다.
    }
    delay(10); // OTA 핸들러가 기민하게 반응하도록 딜레이는 최소화합니다.
}