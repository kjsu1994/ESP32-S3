#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>

#include "config.h"

namespace {
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_DELAY_MS = 500;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
constexpr uint32_t HEALTH_REPORT_INTERVAL_MS = 30000;

Adafruit_NeoPixel pixel(BOARD_RGB_COUNT, BOARD_INTERNAL_RGB_PIN, NEO_GRB + NEO_KHZ800);

bool otaInProgress = false;
bool otaReady = false;
bool deviceRegistered = false;
bool stateDirty = true;
bool lastWifiConnected = false;

uint32_t lastHealthReportAt = 0;
uint32_t lastReconnectAttemptAt = 0;

String deviceId;

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

String boolToJson(bool value) {
    return value ? "true" : "false";
}

String buildApiUrl(const String& path) {
    return String("http://") + HOME_IOT_SERVER_HOST + ":" + String(HOME_IOT_SERVER_PORT) + path;
}

String buildDeviceId() {
    const uint64_t chipId = ESP.getEfuseMac();
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "esp32s3-%04X%08X",
             static_cast<uint16_t>(chipId >> 32),
             static_cast<uint32_t>(chipId));
    return String(buffer);
}

void updateStatusLed() {
    if (otaInProgress) {
        setStatusColor(0, 0, 60);
        return;
    }

    if (WiFi.status() == WL_CONNECTED) {
        setStatusColor(0, 60, 0);
        return;
    }

    const uint32_t now = millis();
    if (now - lastReconnectAttemptAt < 1500) {
        setStatusColor(155, 90, 0);
    } else {
        setStatusColor(60, 0, 0);
    }
}

bool postJson(const String& url, const String& payload) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    const int statusCode = http.POST(payload);
    const String responseBody = http.getString();
    http.end();

    if (statusCode < 200 || statusCode >= 300) {
        Serial.printf("[HTTP] POST %s failed. status=%d body=%s\n",
                      url.c_str(), statusCode, responseBody.c_str());
        return false;
    }

    Serial.printf("[HTTP] POST %s ok. status=%d\n", url.c_str(), statusCode);
    return true;
}

bool registerDevice() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    const String payload =
            String("{\"device_id\":\"") + deviceId +
            "\",\"name\":\"" + DEVICE_NAME +
            "\",\"device_type\":\"" + DEVICE_TYPE +
            "\",\"fw_version\":\"" + FW_VERSION +
            "\",\"ip\":\"" + WiFi.localIP().toString() +
            "\",\"rssi\":" + String(WiFi.RSSI()) +
            "}";

    deviceRegistered = postJson(buildApiUrl("/api/devices/register"), payload);
    return deviceRegistered;
}

bool reportHealth() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    if (!deviceRegistered && !registerDevice()) {
        return false;
    }

    const String payload =
            String("{\"online\":") + boolToJson(true) +
            ",\"ip\":\"" + WiFi.localIP().toString() +
            "\",\"rssi\":" + String(WiFi.RSSI()) +
            "}";

    const bool ok = postJson(buildApiUrl("/api/devices/") + deviceId + "/health", payload);
    if (ok) {
        lastHealthReportAt = millis();
    }
    return ok;
}

bool reportState() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    if (!deviceRegistered && !registerDevice()) {
        return false;
    }

    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    const String rawStateJson =
            String("{\\\"wifi_status\\\":\\\"") + wifiStatusToString(WiFi.status()) +
            "\\\",\\\"ota_ready\\\":" + boolToJson(otaReady) +
            ",\\\"ota_in_progress\\\":" + boolToJson(otaInProgress) +
            ",\\\"rssi\\\":" + String(WiFi.RSSI()) +
            "}";

    const bool ledOn = wifiConnected && !otaInProgress;
    const String payload =
            String("{\"led_on\":") + boolToJson(ledOn) +
            ",\"wifi_connected\":" + boolToJson(wifiConnected) +
            ",\"ota_ready\":" + boolToJson(otaReady) +
            ",\"raw_state_json\":\"" + rawStateJson +
            "\"}";

    const bool ok = postJson(buildApiUrl("/api/devices/") + deviceId + "/state/report", payload);
    if (ok) {
        stateDirty = false;
    }
    return ok;
}

bool configureOta() {
    if (otaReady) {
        return true;
    }

    if (!MDNS.begin(OTA_HOSTNAME)) {
        Serial.println("[OTA] mDNS setup failed");
        return false;
    }

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        otaInProgress = true;
        stateDirty = true;
        Serial.println("[OTA] Update started");
        updateStatusLed();
    });

    ArduinoOTA.onEnd([]() {
        otaInProgress = false;
        stateDirty = true;
        Serial.println("\n[OTA] Update finished. Rebooting...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA] Progress: %u%%\r", (progress * 100U) / total);
    });

    ArduinoOTA.onError([](ota_error_t error) {
        otaInProgress = false;
        stateDirty = true;
        Serial.printf("\n[OTA] Error[%u]\n", error);
        updateStatusLed();
    });

    ArduinoOTA.begin();
    otaReady = true;
    Serial.println("[OTA] Ready");
    return true;
}

bool connectToWifi() {
    Serial.printf("[BOOT] Connecting to Wi-Fi SSID: %s\n", WIFI_SSID);
    lastReconnectAttemptAt = millis();
    updateStatusLed();

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

void handleNetworkSync() {
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;

    if (wifiConnected != lastWifiConnected) {
        lastWifiConnected = wifiConnected;
        stateDirty = true;
        if (!wifiConnected) {
            otaReady = false;
            deviceRegistered = false;
            Serial.println("[WIFI] Connection lost");
        } else {
            Serial.println("[WIFI] Connection restored");
        }
    }

    if (!wifiConnected) {
        const uint32_t now = millis();
        if (now - lastReconnectAttemptAt >= WIFI_RECONNECT_INTERVAL_MS) {
            lastReconnectAttemptAt = now;
            Serial.println("[WIFI] Reconnecting...");
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            updateStatusLed();
        }
        return;
    }

    if (!configureOta()) {
        return;
    }

    if (!deviceRegistered) {
        registerDevice();
    }

    if (stateDirty) {
        reportState();
    }

    const uint32_t now = millis();
    if (now - lastHealthReportAt >= HEALTH_REPORT_INTERVAL_MS) {
        reportHealth();
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(1000);

    deviceId = buildDeviceId();

    pixel.begin();
    pixel.setBrightness(40);
    setStatusColor(60, 0, 0);

    Serial.println();
    Serial.println("[BOOT] ESP32S3 setup started");
    Serial.printf("[BOOT] Chip model: %s\n", ESP.getChipModel());
    Serial.printf("[BOOT] Free heap: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("[BOOT] Device ID: %s\n", deviceId.c_str());

    if (!connectToWifi()) {
        Serial.println("[BOOT] Continuing without Wi-Fi. OTA and Home_IOT sync will retry later.");
    }

    handleNetworkSync();
    updateStatusLed();
}

void loop() {
    if (otaReady) {
        ArduinoOTA.handle();
    }

    handleNetworkSync();
    updateStatusLed();

    if (WiFi.status() != WL_CONNECTED) {
        static uint32_t lastLogAt = 0;
        const uint32_t now = millis();
        if (now - lastLogAt >= 5000) {
            lastLogAt = now;
            Serial.printf("[WIFI] Not connected. Current status=%s (%d)\n",
                          wifiStatusToString(WiFi.status()),
                          static_cast<int>(WiFi.status()));
        }
    }

    delay(10);
}
