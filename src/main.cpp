#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>

#include "config.h"

namespace {
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_DELAY_MS = 500;

Adafruit_NeoPixel pixel(BOARD_RGB_COUNT, BOARD_INTERNAL_RGB_PIN, NEO_GRB + NEO_KHZ800);
bool otaInProgress = false;

void setStatusColor(uint8_t r, uint8_t g, uint8_t b) {
    pixel.setPixelColor(0, pixel.Color(r, g, b));
    pixel.show();
}

const char* wifiStatusToString(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "IDLE";
        case WL_NO_SSID_AVAIL: return "NO_SSID";
        case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
        case WL_CONNECTED: return "CONNECTED";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED: return "DISCONNECTED";
        default: return "UNKNOWN";
    }
}

bool connectToWifi() {
    Serial.printf("[BOOT] Connecting to Wi-Fi SSID: %s\n", WIFI_SSID);
    setStatusColor(255, 180, 0);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const uint32_t startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
        const wl_status_t status = WiFi.status();
        Serial.printf("[WIFI] status=%s (%d), elapsed=%lus\n",
                      wifiStatusToString(status),
                      static_cast<int>(status),
                      static_cast<unsigned long>((millis() - startedAt) / 1000));
        delay(WIFI_RETRY_DELAY_MS);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[WIFI] Connection timed out after %lus. Final status=%s (%d)\n",
                      static_cast<unsigned long>(WIFI_CONNECT_TIMEOUT_MS / 1000),
                      wifiStatusToString(WiFi.status()),
                      static_cast<int>(WiFi.status()));
        return false;
    }

    Serial.println("[WIFI] Connected");
    Serial.print("[WIFI] IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WIFI] RSSI: ");
    Serial.println(WiFi.RSSI());
    Serial.printf("[WIFI] OTA host: %s.local\n", OTA_HOSTNAME);
    return true;
}

bool configureOta() {
    if (!MDNS.begin(OTA_HOSTNAME)) {
        Serial.println("[OTA] mDNS setup failed");
        return false;
    }

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        otaInProgress = true;
        Serial.println("[OTA] Update started");
        setStatusColor(0, 0, 255);
    });

    ArduinoOTA.onEnd([]() {
        otaInProgress = false;
        Serial.println("\n[OTA] Update finished. Rebooting...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress * 100U) / total);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        otaInProgress = false;
        Serial.printf("\n[OTA] Error[%u]\n", error);
        setStatusColor(255, 0, 0);
    });

    ArduinoOTA.begin();
    Serial.println("[OTA] Ready");
    return true;
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(1000);

    pixel.begin();
    pixel.setBrightness(40);
    setStatusColor(255, 0, 0);

    Serial.println();
    Serial.println("[BOOT] ESP32S3 setup started");
    Serial.printf("[BOOT] Chip model: %s\n", ESP.getChipModel());
    Serial.printf("[BOOT] Free heap: %u bytes\n", ESP.getFreeHeap());

    const bool wifiConnected = connectToWifi();
    if (wifiConnected && configureOta()) {
        setStatusColor(0, 255, 0);
    } else if (!wifiConnected) {
        Serial.println("[BOOT] Continuing without Wi-Fi. OTA will not be available.");
    } else {
        Serial.println("[BOOT] Wi-Fi connected, but OTA startup failed.");
    }
}

void loop() {
    ArduinoOTA.handle();

    if (WiFi.status() != WL_CONNECTED) {
        if (!otaInProgress) {
            setStatusColor(255, 0, 0);
        }
        static uint32_t lastLogAt = 0;
        const uint32_t now = millis();
        if (now - lastLogAt >= 5000) {
            lastLogAt = now;
            Serial.printf("[WIFI] Not connected. Current status=%s (%d)\n",
                          wifiStatusToString(WiFi.status()),
                          static_cast<int>(WiFi.status()));
        }
    } else if (!otaInProgress) {
        setStatusColor(0, 255, 0);
    }

    delay(10);
}
