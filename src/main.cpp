#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>

#include "config.h"

namespace {
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_DELAY_MS = 500;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t HEALTH_REPORT_INTERVAL_MS = 30000;
constexpr uint32_t COMMAND_POLL_INTERVAL_MS = 3000;

enum class LedMode {
    STATUS,
    FORCED_ON,
    FORCED_OFF,
};

Adafruit_NeoPixel pixelPrimary(BOARD_RGB_COUNT, BOARD_INTERNAL_RGB_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixelAlternate(BOARD_RGB_COUNT, BOARD_INTERNAL_RGB_ALT_PIN, NEO_GRB + NEO_KHZ800);
WiFiClient mqttNetworkClient;
PubSubClient mqttClient(mqttNetworkClient);

bool otaInProgress = false;
bool otaReady = false;
bool deviceRegistered = false;
bool stateDirty = true;
bool lastWifiConnected = false;
bool rebootRequested = false;
int lastOtaProgressPercent = -1;

uint32_t lastHealthReportAt = 0;
uint32_t lastReconnectAttemptAt = 0;
uint32_t lastMqttReconnectAttemptAt = 0;
uint32_t rebootRequestedAt = 0;
uint32_t lastCommandPollAt = 0;

LedMode ledMode = LedMode::STATUS;
String deviceId;

void setStatusColor(uint8_t r, uint8_t g, uint8_t b) {
    pixelPrimary.setPixelColor(0, pixelPrimary.Color(r, g, b));
    pixelPrimary.show();

    pixelAlternate.setPixelColor(0, pixelAlternate.Color(r, g, b));
    pixelAlternate.show();
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

const char* ledModeToString(LedMode mode) {
    switch (mode) {
        case LedMode::STATUS: return "STATUS";
        case LedMode::FORCED_ON: return "FORCED_ON";
        case LedMode::FORCED_OFF: return "FORCED_OFF";
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

String buildCommandTopic() {
    return String("devices/") + deviceId + "/commands";
}

String escapeJson(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); ++i) {
        const char ch = value[i];
        if (ch == '\\' || ch == '"') {
            escaped += '\\';
        }
        escaped += ch;
    }
    return escaped;
}

String extractJsonString(const String& payload, const char* key) {
    const String token = String("\"") + key + "\":\"";
    const int start = payload.indexOf(token);
    if (start < 0) {
        return "";
    }

    int valueStart = start + token.length();
    String result;
    while (valueStart < payload.length()) {
        const char ch = payload[valueStart++];
        if (ch == '"' && (valueStart < 2 || payload[valueStart - 2] != '\\')) {
            break;
        }
        result += ch;
    }
    return result;
}

long extractJsonLong(const String& payload, const char* key) {
    const String token = String("\"") + key + "\":";
    int start = payload.indexOf(token);
    if (start < 0) {
        return -1;
    }

    start += token.length();
    while (start < payload.length() && payload[start] == ' ') {
        ++start;
    }

    String digits;
    while (start < payload.length()) {
        const char ch = payload[start++];
        if ((ch >= '0' && ch <= '9') || (digits.isEmpty() && ch == '-')) {
            digits += ch;
            continue;
        }
        break;
    }

    return digits.isEmpty() ? -1 : digits.toInt();
}

void updateStatusLed() {
    if (otaInProgress) {
        setStatusColor(0, 0, 60);
        return;
    }

    if (ledMode == LedMode::FORCED_ON) {
        setStatusColor(60, 60, 60);
        return;
    }

    if (ledMode == LedMode::FORCED_OFF) {
        setStatusColor(0, 0, 0);
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

bool patchJson(const String& url, const String& payload) {
    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    const int statusCode = http.PATCH(payload);
    const String responseBody = http.getString();
    http.end();

    if (statusCode < 200 || statusCode >= 300) {
        Serial.printf("[HTTP] PATCH %s failed. status=%d body=%s\n",
                      url.c_str(), statusCode, responseBody.c_str());
        return false;
    }

    Serial.printf("[HTTP] PATCH %s ok. status=%d\n", url.c_str(), statusCode);
    return true;
}

bool getJson(const String& url, String& responseBody) {
    HTTPClient http;
    http.begin(url);

    const int statusCode = http.GET();
    responseBody = http.getString();
    http.end();

    if (statusCode < 200 || statusCode >= 300) {
        Serial.printf("[HTTP] GET %s failed. status=%d body=%s\n",
                      url.c_str(), statusCode, responseBody.c_str());
        return false;
    }

    return true;
}

String buildReportedStateJson() {
    return String("{\"wifi_status\":\"") + wifiStatusToString(WiFi.status()) +
           "\",\"ota_ready\":" + boolToJson(otaReady) +
           ",\"ota_in_progress\":" + boolToJson(otaInProgress) +
           ",\"mqtt_connected\":" + boolToJson(mqttClient.connected()) +
           ",\"led_mode\":\"" + ledModeToString(ledMode) +
           "\",\"rssi\":" + String(WiFi.RSSI()) +
           "}";
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
            ",\"owner_member_id\":" + String(HOME_IOT_OWNER_MEMBER_ID) +
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

    const String rawStateJson = buildReportedStateJson();

    const String payload =
            String("{\"led_on\":") + boolToJson(ledMode == LedMode::FORCED_ON) +
            ",\"wifi_connected\":" + boolToJson(WiFi.status() == WL_CONNECTED) +
            ",\"ota_ready\":" + boolToJson(otaReady) +
            ",\"raw_state_json\":\"" + escapeJson(rawStateJson) +
            "\"}";

    const bool ok = postJson(buildApiUrl("/api/devices/") + deviceId + "/state/report", payload);
    if (ok) {
        stateDirty = false;
    }
    return ok;
}

bool reportOtaStatus(const String& otaStatus, int progressPercent, const String& message) {
    if (WiFi.status() != WL_CONNECTED || !deviceRegistered) {
        return false;
    }

    const String payload =
            String("{\"ota_status\":\"") + otaStatus +
            "\",\"progress_percent\":" + String(progressPercent) +
            ",\"message\":\"" + escapeJson(message) +
            "\",\"target_version\":\"" + FW_VERSION +
            "\"}";

    return postJson(buildApiUrl("/api/devices/") + deviceId + "/ota/status", payload);
}

bool updateCommandStage(long commandId, const String& stage, const String& resultMessage, bool includeReportedState) {
    if (WiFi.status() != WL_CONNECTED || commandId <= 0) {
        return false;
    }

    String payload =
            String("{\"stage\":\"") + stage +
            "\",\"result_message\":\"" + escapeJson(resultMessage) + "\"";

    if (includeReportedState) {
        payload += String(",\"reported_state_json\":\"") + escapeJson(buildReportedStateJson()) + "\"";
    }
    payload += "}";

    return patchJson(buildApiUrl("/api/devices/") + deviceId + "/commands/" + String(commandId), payload);
}

void requestReboot() {
    rebootRequested = true;
    rebootRequestedAt = millis();
    Serial.println("[CMD] Reboot requested");
}

bool handleCommand(const String& commandType, String& resultMessage) {
    Serial.printf("[CMD] Received command: %s\n", commandType.c_str());

    if (commandType == "LED_ON") {
        ledMode = LedMode::FORCED_ON;
        stateDirty = true;
        updateStatusLed();
        resultMessage = "LED forced on";
        return true;
    }

    if (commandType == "LED_OFF") {
        ledMode = LedMode::FORCED_OFF;
        stateDirty = true;
        updateStatusLed();
        resultMessage = "LED forced off";
        return true;
    }

    if (commandType == "REBOOT") {
        requestReboot();
        resultMessage = "Reboot scheduled";
        return true;
    }

    if (commandType == "NETWORK_STATUS") {
        ledMode = LedMode::STATUS;
        stateDirty = true;
        updateStatusLed();
        resultMessage = "LED returned to network status mode";
        return true;
    }

    if (commandType == "OTA_UPDATE") {
        resultMessage = "OTA update must be pushed through ArduinoOTA for this firmware";
        Serial.printf("[CMD] Unsupported OTA trigger: %s\n", commandType.c_str());
        return false;
    }

    Serial.printf("[CMD] Unsupported command: %s\n", commandType.c_str());
    resultMessage = "Unsupported command";
    return false;
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    String message;
    message.reserve(length);
    for (unsigned int i = 0; i < length; ++i) {
        message += static_cast<char>(payload[i]);
    }

    Serial.printf("[MQTT] Message topic=%s payload=%s\n", topic, message.c_str());
    const String commandType = extractJsonString(message, "command_type");
    const long commandId = extractJsonLong(message, "command_id");
    if (commandType.isEmpty()) {
        Serial.println("[MQTT] command_type not found in payload");
        return;
    }

    if (commandId > 0) {
        updateCommandStage(commandId, "RECEIVED", "Command received through MQTT", false);
    }

    String resultMessage;
    const bool applied = handleCommand(commandType, resultMessage);

    if (commandId > 0) {
        updateCommandStage(commandId, applied ? "APPLIED" : "FAILED", resultMessage, true);
    }
}

void pollPendingCommand() {
    if (WiFi.status() != WL_CONNECTED || !deviceRegistered) {
        return;
    }

    String responseBody;
    if (!getJson(buildApiUrl("/api/devices/") + deviceId + "/commands/pending", responseBody)) {
        return;
    }

    const long commandId = extractJsonLong(responseBody, "command_id");
    if (commandId <= 0) {
        return;
    }

    const String commandType = extractJsonString(responseBody, "command_type");
    if (commandType.isEmpty()) {
        updateCommandStage(commandId, "FAILED", "command_type not found in pending command", false);
        return;
    }

    updateCommandStage(commandId, "RECEIVED", "Command received by device poll", false);

    String resultMessage;
    const bool applied = handleCommand(commandType, resultMessage);
    updateCommandStage(commandId, applied ? "APPLIED" : "FAILED", resultMessage, true);
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
        lastOtaProgressPercent = 0;
        stateDirty = true;
        Serial.println("[OTA] Update started");
        reportOtaStatus("RUNNING", 0, "ArduinoOTA update started");
        updateStatusLed();
    });

    ArduinoOTA.onEnd([]() {
        otaInProgress = false;
        lastOtaProgressPercent = -1;
        stateDirty = true;
        Serial.println("\n[OTA] Update finished. Rebooting...");
        reportOtaStatus("SUCCEEDED", 100, "ArduinoOTA update finished");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        const int percent = static_cast<int>((progress * 100U) / total);
        Serial.printf("[OTA] Progress: %u%%\r", percent);
        if (percent == 100 || percent - lastOtaProgressPercent >= 10) {
            lastOtaProgressPercent = percent;
            reportOtaStatus("RUNNING", percent, "ArduinoOTA update in progress");
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        otaInProgress = false;
        lastOtaProgressPercent = -1;
        stateDirty = true;
        Serial.printf("\n[OTA] Error[%u]\n", error);
        reportOtaStatus("FAILED", 0, String("ArduinoOTA error ") + String(static_cast<unsigned int>(error)));
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

bool connectMqtt() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    if (mqttClient.connected()) {
        return true;
    }

    mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    mqttClient.setCallback(onMqttMessage);
    mqttClient.setBufferSize(512);

    const String clientId = deviceId + "-mqtt";
    Serial.printf("[MQTT] Connecting to %s:%d as %s\n",
                  MQTT_BROKER_HOST,
                  MQTT_BROKER_PORT,
                  clientId.c_str());

    if (!mqttClient.connect(clientId.c_str())) {
        Serial.printf("[MQTT] Connect failed. state=%d\n", mqttClient.state());
        return false;
    }

    const String topic = buildCommandTopic();
    if (!mqttClient.subscribe(topic.c_str(), 1)) {
        Serial.printf("[MQTT] Subscribe failed: %s\n", topic.c_str());
        mqttClient.disconnect();
        return false;
    }

    Serial.printf("[MQTT] Subscribed to %s\n", topic.c_str());
    stateDirty = true;
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
            mqttClient.disconnect();
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

    const uint32_t now = millis();
    if (!mqttClient.connected() && now - lastMqttReconnectAttemptAt >= MQTT_RECONNECT_INTERVAL_MS) {
        lastMqttReconnectAttemptAt = now;
        connectMqtt();
    }

    if (mqttClient.connected()) {
        mqttClient.loop();
    }

    if (stateDirty) {
        reportState();
    }

    if (now - lastHealthReportAt >= HEALTH_REPORT_INTERVAL_MS) {
        reportHealth();
    }

    if (now - lastCommandPollAt >= COMMAND_POLL_INTERVAL_MS) {
        lastCommandPollAt = now;
        pollPendingCommand();
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(1000);

    deviceId = buildDeviceId();

    pixelPrimary.begin();
    pixelPrimary.setBrightness(40);
    pixelAlternate.begin();
    pixelAlternate.setBrightness(40);
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

    if (rebootRequested && millis() - rebootRequestedAt >= 500) {
        Serial.println("[BOOT] Rebooting now");
        delay(50);
        ESP.restart();
    }

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
