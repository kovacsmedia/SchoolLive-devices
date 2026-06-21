#include "BackendClient.h"
#include <WiFiClient.h>  // Arduino-ESP32 v3.x: WiFiClient typedef alias for NetworkClient

void BackendClient::begin(const String& baseUrl) {
    _baseUrl = baseUrl;

    if (_baseUrl.endsWith("/")) {
        _baseUrl.remove(_baseUrl.length() - 1);
    }
}

void BackendClient::setDeviceKey(const String& deviceKey) {
    _deviceKey = deviceKey;
}

bool BackendClient::isReady() const {
    return _baseUrl.length() > 0 && _deviceKey.length() > 0;
}

void BackendClient::addCommonHeaders(HTTPClient& http) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-device-key", _deviceKey);
}

void BackendClient::waitCooldown() {
    if (_lastHttpEndMs == 0) return;

    unsigned long elapsed = millis() - _lastHttpEndMs;

    if (elapsed < HTTP_COOLDOWN_MS) {
        delay(HTTP_COOLDOWN_MS - elapsed);
    }
}

// ---------------------------------------------------------------------------
// POST JSON
// ---------------------------------------------------------------------------

bool BackendClient::postJson(
    const String& path,
    const JsonDocument& req,
    JsonDocument& resp,
    int& httpCode
) {
    if (!isReady()) return false;

    waitCooldown();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(8000);

    const String url = _baseUrl + path;

    Serial.printf("[HTTP] POST %s\n", url.c_str());

    if (!http.begin(client, url)) {
        Serial.println("[HTTP] begin() failed");
        return false;
    }

    addCommonHeaders(http);

    String body;
    serializeJson(req, body);

    httpCode = http.POST(body);

    Serial.printf("[HTTP] httpCode: %d\n", httpCode);

    if (httpCode <= 0) {
        Serial.printf("[HTTP] Error: %s\n", http.errorToString(httpCode).c_str());

        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    String responseStr = http.getString();

    Serial.printf("[HTTP] Response: %.300s\n", responseStr.c_str());

    _lastHttpEndMs = millis();
    http.end();

    DeserializationError err = deserializeJson(resp, responseStr);

    if (err) {
        Serial.printf("[HTTP] JSON parse error: %s\n", err.c_str());
        return false;
    }

    return httpCode >= 200 && httpCode < 300;
}

// ---------------------------------------------------------------------------
// GET JSON
// ---------------------------------------------------------------------------

bool BackendClient::getJson(
    const String& path,
    JsonDocument& resp,
    int& httpCode
) {
    if (!isReady()) return false;

    waitCooldown();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(8000);

    const String url = _baseUrl + path;

    Serial.printf("[HTTP] GET %s\n", url.c_str());

    if (!http.begin(client, url)) {
        Serial.println("[HTTP] GET begin() failed");
        return false;
    }

    addCommonHeaders(http);

    httpCode = http.GET();

    Serial.printf("[HTTP] httpCode: %d\n", httpCode);

    if (httpCode <= 0) {
        Serial.printf("[HTTP] Error: %s\n", http.errorToString(httpCode).c_str());

        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    String responseStr = http.getString();

    _lastHttpEndMs = millis();
    http.end();

    DeserializationError err = deserializeJson(resp, responseStr);

    if (err) {
        Serial.printf("[HTTP] GET JSON parse error: %s\n", err.c_str());
        return false;
    }

    return httpCode >= 200 && httpCode < 300;
}

// ---------------------------------------------------------------------------
// downloadFile – hangfájl letöltése LittleFS-re
// ---------------------------------------------------------------------------

bool BackendClient::downloadFile(
    const String& url,
    const String& localPath,
    size_t expectedBytes
) {
    if (LittleFS.exists(localPath)) {
        if (expectedBytes == 0) {
            Serial.printf("[DL] Already exists: %s (skip)\n", localPath.c_str());
            return true;
        }

        File f = LittleFS.open(localPath, "r");

        if (f) {
            size_t existingSize = f.size();
            f.close();

            if (existingSize == expectedBytes) {
                Serial.printf(
                    "[DL] Already exists: %s (%d bytes, skip)\n",
                    localPath.c_str(),
                    existingSize
                );
                return true;
            }

            Serial.printf(
                "[DL] Size mismatch %s: local=%d expected=%d, re-downloading\n",
                localPath.c_str(),
                existingSize,
                expectedBytes
            );
        }
    }

    waitCooldown();

    String fullUrl = url;

    if (!fullUrl.startsWith("http://") && !fullUrl.startsWith("https://")) {
        if (!fullUrl.startsWith("/")) {
            fullUrl = "/" + fullUrl;
        }

        fullUrl = _baseUrl + fullUrl;

        Serial.printf("[DL] Resolved relative URL → %s\n", fullUrl.c_str());
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(15000);

    Serial.printf("[DL] Downloading %s → %s\n", fullUrl.c_str(), localPath.c_str());

    if (!http.begin(client, fullUrl)) {
        Serial.println("[DL] begin() failed");
        return false;
    }

    if (_deviceKey.length() > 0) {
        http.addHeader("x-device-key", _deviceKey);
    }

    int httpCode = http.GET();

    Serial.printf("[DL] httpCode: %d\n", httpCode);

    if (httpCode != 200) {
        Serial.printf("[DL] Error: %s\n", http.errorToString(httpCode).c_str());

        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    WiFiClient* stream = http.getStreamPtr();

    if (!stream) {
        Serial.println("[DL] No stream");

        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    File file = LittleFS.open(localPath, "w");

    if (!file) {
        Serial.printf("[DL] Cannot open for write: %s\n", localPath.c_str());

        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    uint8_t buf[512];
    size_t totalWritten = 0;
    int contentLength = http.getSize();
    unsigned long dlStart = millis();

    while (http.connected() && (contentLength > 0 || contentLength == -1)) {
        size_t avail = stream->available();

        if (avail == 0) {
            if (millis() - dlStart > 15000) {
                Serial.println("[DL] Timeout");
                break;
            }

            delay(1);
            continue;
        }

        size_t toRead = min(avail, sizeof(buf));
        size_t read = stream->readBytes(buf, toRead);

        if (read > 0) {
            file.write(buf, read);
            totalWritten += read;
            dlStart = millis();
        }

        if (contentLength > 0 && (int)totalWritten >= contentLength) {
            break;
        }
    }

    file.close();

    _lastHttpEndMs = millis();
    http.end();

    Serial.printf("[DL] Done: %s (%d bytes)\n", localPath.c_str(), totalWritten);

    if (expectedBytes > 0 && totalWritten != expectedBytes) {
        Serial.printf(
            "[DL] Size mismatch after download: got=%d expected=%d\n",
            totalWritten,
            expectedBytes
        );

        LittleFS.remove(localPath);
        return false;
    }

    return totalWritten > 0;
}

// ---------------------------------------------------------------------------
// setSnapConfig – WS HELLO / BEACON_ACK után hívja a DeviceAgent
// ---------------------------------------------------------------------------

void BackendClient::setSnapConfig(const String& host, uint16_t port, const String& deviceId) {
    _snapHost       = host;
    _snapPort       = port;
    _deviceId       = deviceId;
    _snapConfigValid = host.length() > 0 && port > 0 && deviceId.length() > 0;
    Serial.printf("[SNAPCFG] set → deviceId=%s host=%s port=%u\n",
                  deviceId.c_str(), host.c_str(), port);
}

// ---------------------------------------------------------------------------
// postJsonUnauthed – provisioning
// ---------------------------------------------------------------------------

bool BackendClient::postJsonUnauthed(
    const String& path,
    const JsonDocument& req,
    JsonDocument& resp,
    int& httpCode
) {
    if (_baseUrl.length() == 0) return false;

    waitCooldown();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(7000);

    const String url = _baseUrl + path;

    if (!http.begin(client, url)) {
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    String body;
    serializeJson(req, body);

    httpCode = http.POST(body);

    if (httpCode <= 0) {
        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    String responseStr = http.getString();

    _lastHttpEndMs = millis();
    http.end();

    DeserializationError err = deserializeJson(resp, responseStr);

    if (err) {
        return false;
    }

    return httpCode >= 200 && httpCode < 300;
}

// ---------------------------------------------------------------------------
// confirmProvisioning
// ---------------------------------------------------------------------------

bool BackendClient::confirmProvisioning(
    const String& provisioningToken,
    String& outDeviceKey,
    String& outWifiSsid,
    String& outWifiPass
) {
    outDeviceKey = "";
    outWifiSsid = "";
    outWifiPass = "";

    JsonDocument req;
    req["provisioningToken"] = provisioningToken;

    JsonDocument resp;
    int code = 0;

    bool ok = postJsonUnauthed(
        "/provision/provision/confirm",
        req,
        resp,
        code
    );

    if (!ok) {
        return false;
    }

    if (resp["deviceKey"].is<const char*>()) {
        outDeviceKey = resp["deviceKey"].as<const char*>();
    }

    if (resp["wifi"]["ssid"].is<const char*>()) {
        outWifiSsid = resp["wifi"]["ssid"].as<const char*>();
    }

    if (resp["wifi"]["password"].is<const char*>()) {
        outWifiPass = resp["wifi"]["password"].as<const char*>();
    }

    return outDeviceKey.length() > 0;
}

// ---------------------------------------------------------------------------
// OTA — firmware verziócheck
// ---------------------------------------------------------------------------

bool BackendClient::checkFirmware(
    const String& currentVersion,
    const String& deviceClass,
    const String& hwModel,
    FirmwareCheckResult& outResult
) {
    outResult = FirmwareCheckResult();

    if (!isReady()) return false;

    // Query paraméterek URL-encode-olása minimális (csak alfanumerikus értékek
    // várhatók itt - verzió string mint "S4.4", deviceClass "SPEAKER", hwModel "ESP32_S3").
    String path = "/firmware/check?version=" + currentVersion
                + "&deviceClass=" + deviceClass;
    if (hwModel.length() > 0) {
        path += "&hwModel=" + hwModel;
    }

    JsonDocument resp;
    int code = 0;
    if (!getJson(path, resp, code)) {
        return false;
    }

    outResult.updateAvailable = resp["updateAvailable"] | false;

    if (outResult.updateAvailable && resp["latest"].is<JsonObject>()) {
        JsonObject latest = resp["latest"];
        outResult.version   = latest["version"]   | "";
        outResult.url       = latest["url"]       | "";
        outResult.sizeBytes = latest["sizeBytes"] | 0;
        outResult.sha256    = latest["sha256"]    | "";
        outResult.mandatory = latest["mandatory"] | false;
        outResult.notes     = latest["notes"]     | "";
    }

    return true;
}

// ---------------------------------------------------------------------------
// OTA — folyamatos státuszjelentés a backendre
// ---------------------------------------------------------------------------

bool BackendClient::reportOtaStatus(
    const String& version,
    const String& status,
    int progress,
    const String& errorMsg
) {
    if (!isReady()) return false;

    JsonDocument req;
    req["version"]  = version;
    req["status"]   = status;
    req["progress"] = progress;
    if (errorMsg.length() > 0) {
        req["error"] = errorMsg;
    }

    JsonDocument resp;
    int code = 0;
    return postJson("/firmware/ota-status", req, resp, code);
}