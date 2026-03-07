#include "BackendClient.h"

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
// POST
// ---------------------------------------------------------------------------
bool BackendClient::postJson(const String& path,
                             const JsonDocument& req,
                             JsonDocument& resp,
                             int& httpCode) {
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
    Serial.printf("[HTTP] Response: %.200s\n", responseStr.c_str());
    _lastHttpEndMs = millis();
    http.end();

    DeserializationError err = deserializeJson(resp, responseStr);
    if (err) {
        Serial.printf("[HTTP] JSON parse error: %s\n", err.c_str());
        return false;
    }

    return (httpCode >= 200 && httpCode < 300);
}

// ---------------------------------------------------------------------------
// GET
// ---------------------------------------------------------------------------
bool BackendClient::getJson(const String& path,
                            JsonDocument& resp,
                            int& httpCode) {
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

    return (httpCode >= 200 && httpCode < 300);
}

// ---------------------------------------------------------------------------
// sendBeacon
// ---------------------------------------------------------------------------
bool BackendClient::sendBeacon(uint8_t volume,
                               bool muted,
                               const String& firmwareVersion,
                               const JsonDocument& statusPayload) {
    JsonDocument req;
    req["volume"]          = volume;
    req["muted"]           = muted;
    req["firmwareVersion"] = firmwareVersion;
    req["statusPayload"].set(statusPayload.as<JsonVariantConst>());

    JsonDocument resp;
    int code = 0;
    return postJson("/devices/beacon", req, resp, code);
}

// ---------------------------------------------------------------------------
// poll
// ---------------------------------------------------------------------------
bool BackendClient::poll(PolledCommand& outCmd) {
    outCmd = PolledCommand{};

    JsonDocument req;
    req["ping"] = (uint32_t)millis();

    JsonDocument resp;
    int code = 0;

    bool ok = postJson("/devices/poll", req, resp, code);
    if (!ok) return false;

    if (!resp["ok"].is<bool>() || !resp["ok"].as<bool>()) return false;

    if (resp["command"].isNull()) {
        outCmd.hasCommand = false;
        return true;
    }

    JsonObject cmd    = resp["command"].as<JsonObject>();
    outCmd.hasCommand = true;
    outCmd.id         = cmd["id"].as<String>();
    outCmd.payload.clear();
    outCmd.payload.set(cmd["payload"].as<JsonVariantConst>());

    return true;
}

// ---------------------------------------------------------------------------
// ack
// ---------------------------------------------------------------------------
bool BackendClient::ack(const String& commandId,
                        bool ok,
                        const String& errorMsg) {
    JsonDocument req;
    req["commandId"] = commandId;
    req["ok"]        = ok;
    if (!ok) req["error"] = errorMsg;

    JsonDocument resp;
    int code = 0;
    return postJson("/devices/ack", req, resp, code);
}

// ---------------------------------------------------------------------------
// postJsonUnauthed – provisioning
// ---------------------------------------------------------------------------
bool BackendClient::postJsonUnauthed(const String& path,
                                     const JsonDocument& req,
                                     JsonDocument& resp,
                                     int& httpCode) {
    if (_baseUrl.length() == 0) return false;

    waitCooldown();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(7000);

    const String url = _baseUrl + path;
    if (!http.begin(client, url)) return false;

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
    if (err) return false;

    return (httpCode >= 200 && httpCode < 300);
}

// ---------------------------------------------------------------------------
// confirmProvisioning
// ---------------------------------------------------------------------------
bool BackendClient::confirmProvisioning(const String& provisioningToken,
                                        String& outDeviceKey,
                                        String& outWifiSsid,
                                        String& outWifiPass) {
    outDeviceKey = "";
    outWifiSsid  = "";
    outWifiPass  = "";

    JsonDocument req;
    req["provisioningToken"] = provisioningToken;

    JsonDocument resp;
    int code = 0;

    bool ok = postJsonUnauthed("/provision/provision/confirm", req, resp, code);
    if (!ok) return false;

    if (resp["deviceKey"].is<const char*>())
        outDeviceKey = resp["deviceKey"].as<String>();
    if (resp["wifi"]["ssid"].is<const char*>())
        outWifiSsid = resp["wifi"]["ssid"].as<String>();
    if (resp["wifi"]["password"].is<const char*>())
        outWifiPass = resp["wifi"]["password"].as<String>();

    return outDeviceKey.length() > 0;
}