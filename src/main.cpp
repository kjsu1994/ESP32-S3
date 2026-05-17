#include <Arduino.h>
#include <WiFi.h>              // 내장 와이파이 라이브러리 로드
#include <Adafruit_NeoPixel.h>
#include "config.h"            // 환경 변수 로드

Adafruit_NeoPixel pixel(BOARD_RGB_COUNT, BOARD_INTERNAL_RGB_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
    Serial.begin(115200);

    // LED 초기화 및 최초 상태는 빨간색(🔴 연결 시도 중)으로 설정
    pixel.begin();
    pixel.setBrightness(40);
    pixel.setPixelColor(0, pixel.Color(255, 0, 0)); // RED
    pixel.show();

    Serial.println("\n=================================");
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    // 백엔드의 비동기 커넥션 풀처럼 내장 와이파이 칩셋에 연결 명령 전달
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    // 와이파이가 완전히 연결될 때까지 while 루프로 대기합니다.
    // 연결 시도 중에는 계속 빨간불이 들어와 있습니다.
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    // [성공] 와이파이 연결 성공 시 루프를 탈출하고 초록색(🟢)으로 변경!
    pixel.setPixelColor(0, pixel.Color(0, 60, 0)); // GREEN
    pixel.show();

    Serial.println("\n🌐 WiFi Connected Successfully!");
    Serial.print("📌 Board IP Address: ");
    Serial.println(WiFi.localIP()); // 공유기로부터 할당받은 보드의 가상 IP 주소 출력
    Serial.println("=================================");
}

void loop() {
    // 10초마다 와이파이 연결 상태를 주기적으로 체크하는 헬스 체크(Health Check) 로직
    if (WiFi.status() == WL_CONNECTED) {
        // 연결 유지 중이면 초록불 유지
        pixel.setPixelColor(0, pixel.Color(0, 60, 0));
    } else {
        // 혹시나 공유기가 꺼지거나 연결이 끊어지면 빨간불로 즉시 경고
        Serial.println("⚠️ WiFi Connection Lost!");
        pixel.setPixelColor(0, pixel.Color(60, 0, 0));
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD); // 재연결 시도
    }
    pixel.show();
    delay(10000);
}