#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <time.h>
#ifdef BOARD_HAS_CAMERA
#include "esp_camera.h"
#include <esp_system.h>
#include <mbedtls/base64.h>
#endif

#include "config.h"

namespace {
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_DELAY_MS = 500;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;
constexpr uint32_t HEALTH_REPORT_INTERVAL_MS = 30000;
constexpr uint32_t STATE_REPORT_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t COMMAND_POLL_INTERVAL_MS = 3000;
constexpr uint32_t TELEMETRY_REPORT_INTERVAL_MS = 60000;
constexpr uint32_t TELEMETRY_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t TLS_TIME_SYNC_TIMEOUT_MS = 10000;
#ifdef BOARD_HAS_CAMERA
constexpr uint32_t CAMERA_STATUS_RETRY_INTERVAL_MS = 10000;
constexpr uint32_t CAMERA_DIAGNOSTICS_LOG_INTERVAL_MS = 10000;
#endif
constexpr const char* DEVICE_TOKEN_PREF_NAMESPACE = "homeiot";
constexpr const char* DEVICE_TOKEN_PREF_KEY = "api_token";

enum class LedMode {
    STATUS,
    FORCED_ON,
    FORCED_OFF,
};

Adafruit_NeoPixel pixelPrimary(BOARD_RGB_COUNT, BOARD_INTERNAL_RGB_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel pixelAlternate(BOARD_RGB_COUNT, BOARD_INTERNAL_RGB_ALT_PIN, NEO_GRB + NEO_KHZ800);
WiFiClient mqttNetworkClient;
WiFiClientSecure httpsClient;
PubSubClient mqttClient(mqttNetworkClient);
Preferences preferences;
#ifdef BOARD_HAS_CAMERA
WiFiClient cameraWebSocketClient;
#endif

bool otaInProgress = false;
bool otaReady = false;
bool deviceRegistered = false;
bool stateDirty = true;
bool lastWifiConnected = false;
bool lastMqttConnected = false;
bool rebootRequested = false;
bool bootTelemetrySent = false;
bool wifiReconnectedTelemetryPending = false;
#ifdef BOARD_HAS_CAMERA
bool cameraReady = false;
bool cameraStatusDirty = true;
bool cameraWebSocketStarted = false;
bool cameraWebSocketConnected = false;
bool cameraStreamingEnabled = true;
bool cameraControlReceived = false;
#endif
int lastOtaProgressPercent = -1;

uint32_t lastHealthReportAt = 0;
uint32_t lastReconnectAttemptAt = 0;
uint32_t lastMqttReconnectAttemptAt = 0;
uint32_t rebootRequestedAt = 0;
uint32_t lastCommandPollAt = 0;
uint32_t lastTelemetryReportAt = 0;
uint32_t lastTelemetryAttemptAt = 0;
uint32_t lastStateReportAttemptAt = 0;
#ifdef BOARD_HAS_CAMERA
uint32_t lastCameraUploadAt = 0;
uint32_t lastCameraStatusReportAt = 0;
uint32_t lastCameraStatusAttemptAt = 0;
uint32_t lastCameraWebSocketReconnectAt = 0;
uint32_t lastCameraHttpFallbackAt = 0;
uint32_t lastCameraControlPollAt = 0;
uint32_t lastCameraDiagnosticsLogAt = 0;
uint32_t lastCameraRecoveryAt = 0;
uint32_t currentCameraUploadIntervalMs = CAMERA_IDLE_UPLOAD_INTERVAL_MS;
uint32_t cameraCaptureFailureCount = 0;
uint32_t cameraUploadFailureCount = 0;
#endif

LedMode ledMode = LedMode::STATUS;
String deviceId;
String deviceApiToken;

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

const char* effectiveDeviceType() {
#ifdef BOARD_HAS_CAMERA
    return "ESP32S3_CAMERA";
#else
    return DEVICE_TYPE;
#endif
}

String buildApiUrl(const String& path) {
    String endpoint = path;
    if (endpoint.startsWith("/api/")) {
        endpoint = endpoint.substring(4);
    } else if (endpoint == "/api") {
        endpoint = "";
    }

    String basePath = HOME_IOT_API_BASE_PATH;
    if (basePath.endsWith("/")) {
        basePath.remove(basePath.length() - 1);
    }

    String portPart;
    const bool defaultHttpPort = String(HOME_IOT_API_SCHEME) == "http" && HOME_IOT_SERVER_PORT == 80;
    const bool defaultHttpsPort = String(HOME_IOT_API_SCHEME) == "https" && HOME_IOT_SERVER_PORT == 443;
    if (!defaultHttpPort && !defaultHttpsPort) {
        portPart = ":" + String(HOME_IOT_SERVER_PORT);
    }

    return String(HOME_IOT_API_SCHEME) + "://" + HOME_IOT_SERVER_HOST + portPart + basePath + endpoint;
}

String buildApiPath(const String& path) {
    String endpoint = path;
    if (endpoint.startsWith("/api/")) {
        endpoint = endpoint.substring(4);
    } else if (endpoint == "/api") {
        endpoint = "";
    }

    String basePath = HOME_IOT_API_BASE_PATH;
    if (basePath.endsWith("/")) {
        basePath.remove(basePath.length() - 1);
    }

    return basePath + endpoint;
}

bool beginHttpClient(HTTPClient& http, const String& url) {
    if (String(HOME_IOT_API_SCHEME) == "https") {
        if (HOME_IOT_TLS_ALLOW_INSECURE) {
            httpsClient.setInsecure();
        } else {
            httpsClient.setCACert(HOME_IOT_TLS_CA_CERT);
        }
        return http.begin(httpsClient, url);
    }

    return http.begin(url);
}

bool syncTlsClock() {
    if (String(HOME_IOT_API_SCHEME) != "https") {
        return true;
    }

    Serial.println("[TLS] Syncing clock for certificate validation");
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");

    const uint32_t startedAt = millis();
    time_t now = time(nullptr);
    while (now < 1704067200 && millis() - startedAt < TLS_TIME_SYNC_TIMEOUT_MS) {
        delay(250);
        now = time(nullptr);
    }

    if (now < 1704067200) {
        Serial.println("[TLS] Clock sync failed; HTTPS certificate validation may fail");
        return false;
    }

    Serial.printf("[TLS] Clock synced: %ld\n", static_cast<long>(now));
    return true;
}

String activeDeviceApiKey() {
    if (!deviceApiToken.isEmpty()) {
        return deviceApiToken;
    }
    return HOME_IOT_DEVICE_API_KEY;
}

void addJsonHeaders(HTTPClient& http) {
    http.addHeader("Content-Type", "application/json");
    const String apiKey = activeDeviceApiKey();
    if (!apiKey.isEmpty()) {
        http.addHeader("X-Device-Api-Key", apiKey);
    }
}

void addDeviceAuthHeader(HTTPClient& http) {
    const String apiKey = activeDeviceApiKey();
    if (!apiKey.isEmpty()) {
        http.addHeader("X-Device-Api-Key", apiKey);
    }
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

int postJsonForStatusResponse(const String& url, const String& payload, String& responseBody) {
    HTTPClient http;
    if (!beginHttpClient(http, url)) {
        Serial.printf("[HTTP] POST %s begin failed\n", url.c_str());
        return -1;
    }
    addJsonHeaders(http);

    const int statusCode = http.POST(payload);
    responseBody = http.getString();
    http.end();

    if (statusCode < 200 || statusCode >= 300) {
        Serial.printf("[HTTP] POST %s failed. status=%d body=%s\n",
                      url.c_str(), statusCode, responseBody.c_str());
        return statusCode;
    }

    Serial.printf("[HTTP] POST %s ok. status=%d\n", url.c_str(), statusCode);
    return statusCode;
}

bool postJsonForResponse(const String& url, const String& payload, String& responseBody) {
    const int statusCode = postJsonForStatusResponse(url, payload, responseBody);
    return statusCode >= 200 && statusCode < 300;
}

bool postJson(const String& url, const String& payload) {
    String responseBody;
    return postJsonForResponse(url, payload, responseBody);
}

bool patchJson(const String& url, const String& payload) {
    HTTPClient http;
    if (!beginHttpClient(http, url)) {
        Serial.printf("[HTTP] PATCH %s begin failed\n", url.c_str());
        return false;
    }
    addJsonHeaders(http);

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
    if (!beginHttpClient(http, url)) {
        Serial.printf("[HTTP] GET %s begin failed\n", url.c_str());
        return false;
    }
    const String apiKey = activeDeviceApiKey();
    if (!apiKey.isEmpty()) {
        http.addHeader("X-Device-Api-Key", apiKey);
    }

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
    String payload = String("{\"wifi_status\":\"") + wifiStatusToString(WiFi.status()) +
           "\",\"ota_ready\":" + boolToJson(otaReady) +
           ",\"ota_in_progress\":" + boolToJson(otaInProgress) +
           ",\"mqtt_connected\":" + boolToJson(mqttClient.connected()) +
           ",\"led_mode\":\"" + ledModeToString(ledMode) +
           "\",\"rssi\":" + String(WiFi.RSSI());
#ifdef BOARD_HAS_CAMERA
    payload += String(",\"camera_ready\":") + boolToJson(cameraReady);
#endif
    payload += "}";
    return payload;
}

String buildBootTelemetryJson() {
    return String("{\"fw_version\":\"") + FW_VERSION +
           "\",\"device_type\":\"" + effectiveDeviceType() +
           "\",\"ip\":\"" + WiFi.localIP().toString() +
           "\",\"rssi\":" + String(WiFi.RSSI()) +
           ",\"free_heap\":" + String(ESP.getFreeHeap()) +
           "}";
}

bool registerDevice() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    const String payload =
            String("{\"device_id\":\"") + deviceId +
            "\",\"name\":\"" + DEVICE_NAME +
            "\",\"device_type\":\"" + effectiveDeviceType() +
            "\",\"fw_version\":\"" + FW_VERSION +
            "\",\"ip\":\"" + WiFi.localIP().toString() +
            "\",\"rssi\":" + String(WiFi.RSSI()) +
            ",\"owner_member_id\":" + String(HOME_IOT_OWNER_MEMBER_ID) +
            "}";

    String responseBody;
    const String registerUrl = buildApiUrl("/api/devices/register");
    int statusCode = postJsonForStatusResponse(registerUrl, payload, responseBody);
    if ((statusCode == 401 || statusCode == 403) && !deviceApiToken.isEmpty()) {
        Serial.println("[AUTH] Stored device API key rejected; clearing token and retrying bootstrap registration");
        deviceApiToken = "";
        preferences.remove(DEVICE_TOKEN_PREF_KEY);
        statusCode = postJsonForStatusResponse(registerUrl, payload, responseBody);
    }

    deviceRegistered = statusCode >= 200 && statusCode < 300;
    if (deviceRegistered) {
        const String issuedKey = extractJsonString(responseBody, "device_api_key");
        if (!issuedKey.isEmpty() && issuedKey != deviceApiToken) {
            deviceApiToken = issuedKey;
            preferences.putString(DEVICE_TOKEN_PREF_KEY, deviceApiToken);
            Serial.println("[AUTH] Stored device-specific API key");
        }
    }
    return deviceRegistered;
}

bool reportHealth() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    lastHealthReportAt = millis();

    if (!deviceRegistered && !registerDevice()) {
        return false;
    }

    const String payload =
            String("{\"online\":") + boolToJson(true) +
            ",\"ip\":\"" + WiFi.localIP().toString() +
            "\",\"rssi\":" + String(WiFi.RSSI()) +
            "}";

    const bool ok = postJson(buildApiUrl("/api/devices/") + deviceId + "/health", payload);
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

bool reportTelemetry(const String& eventType, const String& payloadJson) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    const uint32_t now = millis();
    if (now - lastTelemetryAttemptAt < TELEMETRY_RETRY_INTERVAL_MS) {
        return false;
    }
    lastTelemetryAttemptAt = now;

    if (!deviceRegistered && !registerDevice()) {
        return false;
    }

    const String payload =
            String("{\"event_type\":\"") + eventType +
            "\",\"payload_json\":\"" + escapeJson(payloadJson) +
            "\"}";

    return postJson(buildApiUrl("/api/devices/") + deviceId + "/telemetry", payload);
}

#ifdef BOARD_HAS_CAMERA
bool initializeCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = CAMERA_PIN_Y2;
    config.pin_d1 = CAMERA_PIN_Y3;
    config.pin_d2 = CAMERA_PIN_Y4;
    config.pin_d3 = CAMERA_PIN_Y5;
    config.pin_d4 = CAMERA_PIN_Y6;
    config.pin_d5 = CAMERA_PIN_Y7;
    config.pin_d6 = CAMERA_PIN_Y8;
    config.pin_d7 = CAMERA_PIN_Y9;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    config.pin_sccb_sda = CAMERA_PIN_SIOD;
    config.pin_sccb_scl = CAMERA_PIN_SIOC;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = CAMERA_XCLK_FREQ_HZ;
    config.frame_size = CAMERA_FRAME_SIZE;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.jpeg_quality = CAMERA_JPEG_QUALITY;
    config.fb_count = 1;

    const esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        cameraReady = false;
        cameraStatusDirty = true;
        Serial.printf("[CAMERA] Init failed: 0x%x\n", err);
        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
        sensor->set_framesize(sensor, CAMERA_FRAME_SIZE);
        sensor->set_quality(sensor, CAMERA_JPEG_QUALITY);
        sensor->set_vflip(sensor, CAMERA_VERTICAL_FLIP ? 1 : 0);
        sensor->set_hmirror(sensor, CAMERA_HORIZONTAL_MIRROR ? 1 : 0);
    }

    cameraReady = true;
    cameraStatusDirty = true;
    Serial.println("[CAMERA] Ready");
    return true;
}

bool runCameraSelfTest(const char* label) {
    if (!cameraReady) {
        Serial.printf("[CAMERA] Self-test skipped label=%s ready=false\n", label);
        return false;
    }

    const uint32_t startedAt = millis();
    camera_fb_t* frame = esp_camera_fb_get();
    const uint32_t elapsed = millis() - startedAt;
    if (frame == nullptr) {
        Serial.printf("[CAMERA] Self-test failed label=%s elapsed_ms=%lu heap=%u psram=%s\n",
                      label,
                      static_cast<unsigned long>(elapsed),
                      ESP.getFreeHeap(),
                      psramFound() ? "yes" : "no");
        return false;
    }

    Serial.printf("[CAMERA] Self-test ok label=%s size=%ux%u bytes=%u elapsed_ms=%lu format=%d heap=%u\n",
                  label,
                  frame->width,
                  frame->height,
                  static_cast<unsigned int>(frame->len),
                  static_cast<unsigned long>(elapsed),
                  static_cast<int>(frame->format),
                  ESP.getFreeHeap());
    esp_camera_fb_return(frame);
    return true;
}

bool recoverCamera(const char* reason) {
    const uint32_t now = millis();
    if (lastCameraRecoveryAt != 0 && now - lastCameraRecoveryAt < CAMERA_CAPTURE_RECOVERY_COOLDOWN_MS) {
        return false;
    }
    lastCameraRecoveryAt = now;

    Serial.printf("[CAMERA] Recovery start reason=%s failures=%lu heap=%u\n",
                  reason,
                  static_cast<unsigned long>(cameraCaptureFailureCount),
                  ESP.getFreeHeap());

    if (cameraWebSocketConnected) {
        cameraWebSocketClient.stop();
        cameraWebSocketConnected = false;
    }

    if (cameraReady) {
        esp_camera_deinit();
        cameraReady = false;
        delay(200);
    }

    const bool initialized = initializeCamera();
    if (!initialized) {
        cameraStatusDirty = true;
        Serial.printf("[CAMERA] Recovery init failed reason=%s\n", reason);
        return false;
    }

    delay(200);
    const bool selfTestOk = runCameraSelfTest(reason);
    if (selfTestOk) {
        cameraCaptureFailureCount = 0;
        cameraStatusDirty = true;
        Serial.printf("[CAMERA] Recovery ok reason=%s\n", reason);
        return true;
    }

    cameraStatusDirty = true;
    Serial.printf("[CAMERA] Recovery self-test failed reason=%s\n", reason);
    return false;
}

bool reportCameraStatus(const String& message) {
    if (WiFi.status() != WL_CONNECTED || !deviceRegistered) {
        return false;
    }
    lastCameraStatusAttemptAt = millis();

    const String payload =
            String("{\"camera_ready\":") + boolToJson(cameraReady) +
            ",\"message\":\"" + escapeJson(message) + "\"}";

    const bool ok = postJson(buildApiUrl("/api/devices/") + deviceId + "/camera/status", payload);
    if (ok) {
        cameraStatusDirty = false;
        lastCameraStatusReportAt = millis();
    }
    return ok;
}

void applyCameraControl(const String& payload) {
    const String mode = extractJsonString(payload, "mode");
    const long interval = extractJsonLong(payload, "upload_interval_ms");
    const long viewerCount = extractJsonLong(payload, "viewer_count");
    const bool nextStreamingEnabled = mode != "OFF";
    const uint32_t previousInterval = currentCameraUploadIntervalMs;
    const bool previousStreamingEnabled = cameraStreamingEnabled;

    if (interval >= 100 && interval <= 10000) {
        currentCameraUploadIntervalMs = static_cast<uint32_t>(interval);
    }
    cameraStreamingEnabled = nextStreamingEnabled;
    cameraControlReceived = true;

    if (previousInterval != currentCameraUploadIntervalMs || previousStreamingEnabled != cameraStreamingEnabled) {
        Serial.printf("[CAMERA] Control mode=%s viewers=%ld interval=%lu streaming=%s\n",
                      mode.isEmpty() ? "UNKNOWN" : mode.c_str(),
                      viewerCount,
                      static_cast<unsigned long>(currentCameraUploadIntervalMs),
                      cameraStreamingEnabled ? "on" : "off");
    }
}

void pollCameraControl() {
    if (WiFi.status() != WL_CONNECTED || !deviceRegistered) {
        return;
    }
    lastCameraControlPollAt = millis();

    String responseBody;
    const String url = buildApiUrl("/api/devices/") + deviceId + "/camera/control";
    if (!getJson(url, responseBody)) {
        return;
    }

    applyCameraControl(responseBody);
}

String base64EncodeBytes(const uint8_t* data, size_t length) {
    size_t encodedLength = 0;
    mbedtls_base64_encode(nullptr, 0, &encodedLength, data, length);
    char* encoded = static_cast<char*>(malloc(encodedLength + 1));
    if (encoded == nullptr) {
        return "";
    }

    size_t actualLength = 0;
    const int result = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded),
                                            encodedLength + 1,
                                            &actualLength,
                                            data,
                                            length);
    if (result != 0) {
        free(encoded);
        return "";
    }

    encoded[actualLength] = '\0';
    String value(encoded);
    free(encoded);
    return value;
}

String buildWebSocketKey() {
    uint8_t bytes[16];
    for (uint8_t i = 0; i < sizeof(bytes); i += 4) {
        const uint32_t randomValue = esp_random();
        bytes[i] = static_cast<uint8_t>((randomValue >> 24) & 0xFF);
        bytes[i + 1] = static_cast<uint8_t>((randomValue >> 16) & 0xFF);
        bytes[i + 2] = static_cast<uint8_t>((randomValue >> 8) & 0xFF);
        bytes[i + 3] = static_cast<uint8_t>(randomValue & 0xFF);
    }
    return base64EncodeBytes(bytes, sizeof(bytes));
}

String readCameraWebSocketLine(uint32_t timeoutMs) {
    String line;
    const uint32_t startedAt = millis();
    while (millis() - startedAt < timeoutMs) {
        while (cameraWebSocketClient.available()) {
            const char character = static_cast<char>(cameraWebSocketClient.read());
            if (character == '\n') {
                if (line.endsWith("\r")) {
                    line.remove(line.length() - 1);
                }
                return line;
            }
            line += character;
            if (line.length() > 512) {
                return line;
            }
        }
        delay(1);
    }
    return line;
}

bool writeCameraWebSocketBytes(const uint8_t* data, size_t length) {
    size_t written = 0;
    uint32_t lastProgressAt = millis();
    while (written < length) {
        if (!cameraWebSocketClient.connected()) {
            return false;
        }

        const size_t count = cameraWebSocketClient.write(data + written, length - written);
        if (count > 0) {
            written += count;
            lastProgressAt = millis();
            continue;
        }

        if (millis() - lastProgressAt > 3000) {
            return false;
        }
        delay(1);
    }
    return true;
}

void handleCameraWebSocket() {
    if (!cameraWebSocketConnected) {
        return;
    }

    if (!cameraWebSocketClient.connected()) {
        cameraWebSocketClient.stop();
        cameraWebSocketConnected = false;
        cameraWebSocketStarted = false;
        Serial.println("[CAMERA] WebSocket disconnected");
        return;
    }

    while (cameraWebSocketClient.available()) {
        cameraWebSocketClient.read();
    }
}

bool connectCameraWebSocket() {
    if (!CAMERA_USE_WEBSOCKET || WiFi.status() != WL_CONNECTED || !deviceRegistered) {
        return false;
    }
    if (String(HOME_IOT_API_SCHEME) != "http") {
        return false;
    }

    if (cameraWebSocketConnected && cameraWebSocketClient.connected()) {
        return true;
    }

    const uint32_t now = millis();
    if (cameraWebSocketStarted && now - lastCameraWebSocketReconnectAt < CAMERA_WEBSOCKET_RECONNECT_INTERVAL_MS) {
        return cameraWebSocketConnected;
    }

    lastCameraWebSocketReconnectAt = now;
    const String path = buildApiPath("/api/devices/") + deviceId + "/camera/ws";
    const String apiKey = activeDeviceApiKey();
    cameraWebSocketStarted = true;
    Serial.printf("[CAMERA] WebSocket connecting ws://%s:%d%s\n",
                  HOME_IOT_SERVER_HOST,
                  HOME_IOT_SERVER_PORT,
                  path.c_str());

    cameraWebSocketConnected = false;
    cameraWebSocketClient.stop();
    cameraWebSocketClient.setTimeout(CAMERA_WEBSOCKET_HANDSHAKE_TIMEOUT_MS);
    if (!cameraWebSocketClient.connect(HOME_IOT_SERVER_HOST, HOME_IOT_SERVER_PORT)) {
        Serial.println("[CAMERA] WebSocket TCP connect failed");
        return false;
    }
    cameraWebSocketClient.setNoDelay(true);

    const String webSocketKey = buildWebSocketKey();
    if (webSocketKey.isEmpty()) {
        cameraWebSocketClient.stop();
        return false;
    }

    cameraWebSocketClient.print(String("GET ") + path + " HTTP/1.1\r\n");
    cameraWebSocketClient.print(String("Host: ") + HOME_IOT_SERVER_HOST + ":" + String(HOME_IOT_SERVER_PORT) + "\r\n");
    cameraWebSocketClient.print("Upgrade: websocket\r\n");
    cameraWebSocketClient.print("Connection: Upgrade\r\n");
    cameraWebSocketClient.print("Sec-WebSocket-Version: 13\r\n");
    cameraWebSocketClient.print(String("Sec-WebSocket-Key: ") + webSocketKey + "\r\n");
    if (!apiKey.isEmpty()) {
        cameraWebSocketClient.print(String("X-Device-Api-Key: ") + apiKey + "\r\n");
    }
    cameraWebSocketClient.print("\r\n");

    const String statusLine = readCameraWebSocketLine(CAMERA_WEBSOCKET_HANDSHAKE_TIMEOUT_MS);
    if (!statusLine.startsWith("HTTP/1.1 101") && !statusLine.startsWith("HTTP/1.0 101")) {
        Serial.printf("[CAMERA] WebSocket handshake failed: %s\n", statusLine.c_str());
        cameraWebSocketClient.stop();
        return false;
    }

    while (cameraWebSocketClient.connected()) {
        const String header = readCameraWebSocketLine(CAMERA_WEBSOCKET_HANDSHAKE_TIMEOUT_MS);
        if (header.length() == 0) {
            break;
        }
    }

    cameraWebSocketConnected = true;
    Serial.println("[CAMERA] WebSocket connected");
    return true;
}

bool sendCameraFrameWebSocket(camera_fb_t* frame) {
    if (!CAMERA_USE_WEBSOCKET || !cameraWebSocketConnected || !cameraWebSocketClient.connected()) {
        return false;
    }

    const uint64_t payloadLength = static_cast<uint64_t>(frame->len) + 4;
    uint8_t header[18];
    size_t headerLength = 0;
    header[headerLength++] = 0x82;
    if (payloadLength < 126) {
        header[headerLength++] = 0x80 | static_cast<uint8_t>(payloadLength);
    } else if (payloadLength <= 0xFFFF) {
        header[headerLength++] = 0x80 | 126;
        header[headerLength++] = static_cast<uint8_t>((payloadLength >> 8) & 0xFF);
        header[headerLength++] = static_cast<uint8_t>(payloadLength & 0xFF);
    } else {
        header[headerLength++] = 0x80 | 127;
        for (int shift = 56; shift >= 0; shift -= 8) {
            header[headerLength++] = static_cast<uint8_t>((payloadLength >> shift) & 0xFF);
        }
    }

    uint8_t mask[4];
    const uint32_t maskValue = esp_random();
    mask[0] = static_cast<uint8_t>((maskValue >> 24) & 0xFF);
    mask[1] = static_cast<uint8_t>((maskValue >> 16) & 0xFF);
    mask[2] = static_cast<uint8_t>((maskValue >> 8) & 0xFF);
    mask[3] = static_cast<uint8_t>(maskValue & 0xFF);
    memcpy(header + headerLength, mask, sizeof(mask));
    headerLength += sizeof(mask);

    uint8_t metadata[4] = {
            static_cast<uint8_t>((frame->width >> 8) & 0xFF),
            static_cast<uint8_t>(frame->width & 0xFF),
            static_cast<uint8_t>((frame->height >> 8) & 0xFF),
            static_cast<uint8_t>(frame->height & 0xFF),
    };
    for (size_t i = 0; i < sizeof(metadata); i++) {
        metadata[i] ^= mask[i % 4];
    }
    memcpy(header + headerLength, metadata, sizeof(metadata));
    headerLength += sizeof(metadata);

    if (!writeCameraWebSocketBytes(header, headerLength)) {
        cameraWebSocketClient.stop();
        cameraWebSocketConnected = false;
        return false;
    }

    uint8_t chunk[4096];
    size_t offset = 0;
    while (offset < frame->len) {
        const size_t count = min(sizeof(chunk), frame->len - offset);
        for (size_t i = 0; i < count; i++) {
            chunk[i] = frame->buf[offset + i] ^ mask[(offset + i + 4) % 4];
        }
        if (!writeCameraWebSocketBytes(chunk, count)) {
            cameraWebSocketClient.stop();
            cameraWebSocketConnected = false;
            return false;
        }
        offset += count;
    }

    return true;
}

bool postCameraFrameHttp(camera_fb_t* frame) {
    HTTPClient http;
    const String url = buildApiUrl("/api/devices/") + deviceId + "/camera/frame";
    if (!beginHttpClient(http, url)) {
        Serial.printf("[CAMERA] POST %s begin failed\n", url.c_str());
        return false;
    }

    addDeviceAuthHeader(http);
    http.addHeader("Content-Type", "image/jpeg");
    http.addHeader("X-Camera-Width", String(frame->width));
    http.addHeader("X-Camera-Height", String(frame->height));

    const int statusCode = http.POST(frame->buf, frame->len);
    const String responseBody = http.getString();
    http.end();

    if (statusCode < 200 || statusCode >= 300) {
        Serial.printf("[CAMERA] Frame upload failed. status=%d body=%s\n", statusCode, responseBody.c_str());
        return false;
    }

    return true;
}

bool uploadCameraFrame() {
    if (!cameraReady || WiFi.status() != WL_CONNECTED || !deviceRegistered) {
        return false;
    }
    if (!cameraStreamingEnabled) {
        lastCameraUploadAt = millis();
        return false;
    }

    const uint32_t startedAt = millis();
    const uint32_t captureStartedAt = startedAt;
    camera_fb_t* frame = esp_camera_fb_get();
    if (frame == nullptr) {
        cameraCaptureFailureCount++;
        if (cameraCaptureFailureCount == 1) {
            cameraStatusDirty = true;
        }
        Serial.printf("[CAMERA] Frame capture failed count=%lu\n",
                      static_cast<unsigned long>(cameraCaptureFailureCount));
        if (cameraCaptureFailureCount >= CAMERA_CAPTURE_RECOVERY_FAILURES) {
            recoverCamera("capture-fail");
        }
        lastCameraUploadAt = millis();
        return false;
    }
    cameraCaptureFailureCount = 0;
    const uint32_t capturedAt = millis();

    if (frame->format != PIXFORMAT_JPEG) {
        esp_camera_fb_return(frame);
        Serial.println("[CAMERA] Captured frame is not JPEG");
        return false;
    }

    bool ok = false;
    bool triedHttp = false;
    if (CAMERA_USE_WEBSOCKET) {
        connectCameraWebSocket();
        handleCameraWebSocket();
        ok = sendCameraFrameWebSocket(frame);
    }
    if (!ok) {
        const uint32_t now = millis();
        if (CAMERA_USE_WEBSOCKET && (!CAMERA_HTTP_FALLBACK_ENABLED || now - lastCameraHttpFallbackAt < CAMERA_HTTP_FALLBACK_INTERVAL_MS)) {
            esp_camera_fb_return(frame);
            lastCameraUploadAt = now;
            return false;
        }
        lastCameraHttpFallbackAt = now;
        triedHttp = true;
        ok = postCameraFrameHttp(frame);
    }

    const size_t frameLength = frame->len;
    const uint16_t frameWidth = frame->width;
    const uint16_t frameHeight = frame->height;
    esp_camera_fb_return(frame);

    if (ok) {
        cameraUploadFailureCount = 0;
        lastCameraUploadAt = millis();
    } else {
        cameraUploadFailureCount++;
        lastCameraUploadAt = millis();
    }

    const uint32_t finishedAt = millis();
    if (finishedAt - lastCameraDiagnosticsLogAt >= CAMERA_DIAGNOSTICS_LOG_INTERVAL_MS) {
        lastCameraDiagnosticsLogAt = finishedAt;
        Serial.printf("[CAMERA] Frame ok=%s transport=%s size=%ux%u bytes=%u capture_ms=%lu upload_ms=%lu total_ms=%lu interval_ms=%lu upload_failures=%lu heap=%u\n",
                      ok ? "true" : "false",
                      triedHttp ? "http" : "websocket",
                      frameWidth,
                      frameHeight,
                      static_cast<unsigned int>(frameLength),
                      static_cast<unsigned long>(capturedAt - captureStartedAt),
                      static_cast<unsigned long>(finishedAt - capturedAt),
                      static_cast<unsigned long>(finishedAt - startedAt),
                      static_cast<unsigned long>(currentCameraUploadIntervalMs),
                      static_cast<unsigned long>(cameraUploadFailureCount),
                      ESP.getFreeHeap());
    }
    return ok;
}
#endif

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
#ifdef BOARD_HAS_CAMERA
    WiFi.setSleep(false);
#endif
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
    syncTlsClock();
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
            bootTelemetrySent = false;
            lastMqttConnected = false;
            mqttClient.disconnect();
#ifdef BOARD_HAS_CAMERA
            cameraWebSocketClient.stop();
            cameraWebSocketConnected = false;
            cameraWebSocketStarted = false;
#endif
            Serial.println("[WIFI] Connection lost");
        } else {
            wifiReconnectedTelemetryPending = true;
            Serial.println("[WIFI] Connection restored");
        }
    }

    if (!wifiConnected) {
        const uint32_t now = millis();
        if (now - lastReconnectAttemptAt >= WIFI_RECONNECT_INTERVAL_MS) {
            lastReconnectAttemptAt = now;
            Serial.println("[WIFI] Reconnecting...");
            WiFi.disconnect();
#ifdef BOARD_HAS_CAMERA
            WiFi.setSleep(false);
#endif
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

    if (deviceRegistered && !bootTelemetrySent) {
        if (reportTelemetry("boot", buildBootTelemetryJson())) {
            bootTelemetrySent = true;
        }
    }

    if (deviceRegistered && wifiReconnectedTelemetryPending) {
        if (reportTelemetry("wifi_reconnected", buildReportedStateJson())) {
            wifiReconnectedTelemetryPending = false;
        }
    }

    if (!mqttClient.connected() && now - lastMqttReconnectAttemptAt >= MQTT_RECONNECT_INTERVAL_MS) {
        lastMqttReconnectAttemptAt = now;
        connectMqtt();
    }

    if (mqttClient.connected()) {
        mqttClient.loop();
    }

    if (stateDirty && now - lastStateReportAttemptAt >= STATE_REPORT_RETRY_INTERVAL_MS) {
        lastStateReportAttemptAt = now;
        reportState();
    }

    const bool mqttConnected = mqttClient.connected();
    if (mqttConnected != lastMqttConnected) {
        lastMqttConnected = mqttConnected;
        stateDirty = true;
        reportTelemetry(mqttConnected ? "mqtt_connected" : "mqtt_disconnected", buildReportedStateJson());
    }

    if (now - lastHealthReportAt >= HEALTH_REPORT_INTERVAL_MS) {
        reportHealth();
    }

    if (now - lastTelemetryReportAt >= TELEMETRY_REPORT_INTERVAL_MS) {
        if (reportTelemetry("heartbeat", buildReportedStateJson())) {
            lastTelemetryReportAt = now;
        }
    }

#ifdef BOARD_HAS_CAMERA
    if (CAMERA_USE_WEBSOCKET && cameraWebSocketStarted) {
        handleCameraWebSocket();
    }

    if (deviceRegistered
            && ((cameraStatusDirty && now - lastCameraStatusAttemptAt >= CAMERA_STATUS_RETRY_INTERVAL_MS)
                || now - lastCameraStatusReportAt >= CAMERA_STATUS_RETRY_INTERVAL_MS)) {
        reportCameraStatus(cameraReady ? "Camera ready" : "Camera unavailable");
    }

    if (deviceRegistered && (lastCameraControlPollAt == 0 || now - lastCameraControlPollAt >= CAMERA_CONTROL_POLL_INTERVAL_MS)) {
        pollCameraControl();
    }

    if (deviceRegistered && cameraReady && cameraControlReceived && now - lastCameraUploadAt >= currentCameraUploadIntervalMs) {
        uploadCameraFrame();
    }
#endif

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
    preferences.begin(DEVICE_TOKEN_PREF_NAMESPACE, false);
    deviceApiToken = preferences.getString(DEVICE_TOKEN_PREF_KEY, "");

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
    Serial.printf("[AUTH] Device-specific API key: %s\n", deviceApiToken.isEmpty() ? "not provisioned" : "provisioned");

#ifdef BOARD_HAS_CAMERA
    if (initializeCamera() && !runCameraSelfTest("boot")) {
        recoverCamera("boot-retry");
    }
#endif

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
