// BackendClient.cpp – SchoolLive S3.54
// Változások S3.52 → S3.54:
//   • _notifyActivity() segédfüggvény – _ui->setNetActivity() ha _ui != nullptr
//   • postJson, getJson, downloadFile, postJsonUnauthed hívja _notifyActivity()-t
//     http.begin() ELŐTT (már a kérés indításakor látszódjon az aktivitás)

#include "BackendClient.h"
#include "UIManager.h"
#include <LittleFS.h>

// ── UI aktivitás jelzés ───────────────────────────────────────────────────────
void BackendClient::_notifyActivity() {
    if (_ui) _ui->setNetActivity();
}

// ── Inicializáció ─────────────────────────────────────────────────────────────
void BackendClient::begin(const String& baseUrl) {
    _baseUrl = baseUrl;
}

void BackendClient::setDeviceKey(const String& deviceKey) { _deviceKey = deviceKey; }

bool BackendClient::isReady() const { return _baseUrl.length() > 0 && _deviceKey.length() > 0; }

void BackendClient::addCommonHeaders(HTTPClient& http) {
    http.addHeader("Content-Type", "application/json");
    if (_deviceKey.length() > 0) http.addHeader("x-device-key", _deviceKey);
}

void BackendClient::waitCooldown() {
    if (_lastHttpEndMs == 0) return;
    unsigned long elapsed = millis() - _lastHttpEndMs;
    if (elapsed < HTTP_COOLDOWN_MS) delay(HTTP_COOLDOWN_MS - elapsed);
}

// ── fetchSnapPort ─────────────────────────────────────────────────────────────
uint16_t BackendClient::fetchSnapPort() {
    if (!isReady()) return 0;
    JsonDocument resp;
    int code = 0;
    bool ok = getJson("/devices/native/snap-port", resp, code);
    if (!ok || code != 200) {
        Serial.printf("[BACKEND] fetchSnapPort hiba: %d\n", code);
        return 0;
    }
    uint16_t port = resp["snapPort"] | (uint16_t)0;
    Serial.printf("[BACKEND] snapPort: %d\n", port);
    return port;
}

// ── postJson ──────────────────────────────────────────────────────────────────
bool BackendClient::postJson(const String& path, const JsonDocument& req,
                              JsonDocument& resp, int& httpCode) {
    if (_baseUrl.length() == 0) return false;
    waitCooldown();
    _notifyActivity();                          // ← UI aktivitás jelzés
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setTimeout(8000);
    const String url = _baseUrl + path;
    if (!http.begin(client, url)) { _lastHttpEndMs = millis(); return false; }
    addCommonHeaders(http);
    String body; serializeJson(req, body);
    httpCode = http.POST(body);
    if (httpCode <= 0) {
        _lastHttpEndMs = millis(); http.end(); return false;
    }
    String responseStr = http.getString();
    _lastHttpEndMs = millis(); http.end();
    DeserializationError err = deserializeJson(resp, responseStr);
    if (err) { Serial.printf("[HTTP] JSON parse error: %s\n", err.c_str()); return false; }
    return true;
}

// ── getJson ───────────────────────────────────────────────────────────────────
bool BackendClient::getJson(const String& path, JsonDocument& resp, int& httpCode) {
    if (_baseUrl.length() == 0) return false;
    waitCooldown();
    _notifyActivity();                          // ← UI aktivitás jelzés
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setTimeout(8000);
    const String url = _baseUrl + path;
    if (!http.begin(client, url)) { _lastHttpEndMs = millis(); return false; }
    addCommonHeaders(http);
    httpCode = http.GET();
    if (httpCode <= 0) {
        _lastHttpEndMs = millis(); http.end(); return false;
    }
    String responseStr = http.getString();
    _lastHttpEndMs = millis(); http.end();
    DeserializationError err = deserializeJson(resp, responseStr);
    if (err) { Serial.printf("[HTTP] GET JSON parse error: %s\n", err.c_str()); return false; }
    return true;
}

// ── downloadFile ──────────────────────────────────────────────────────────────
bool BackendClient::downloadFile(const String& url, const String& localPath,
                                  size_t expectedBytes) {
    waitCooldown();
    _notifyActivity();                          // ← UI aktivitás jelzés
    String fullUrl = url;
    if (!fullUrl.startsWith("http")) fullUrl = _baseUrl + fullUrl;
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setTimeout(15000);
    if (!http.begin(client, fullUrl)) { _lastHttpEndMs = millis(); return false; }
    if (_deviceKey.length() > 0) http.addHeader("x-device-key", _deviceKey);
    int httpCode = http.GET();
    if (httpCode != 200) { _lastHttpEndMs = millis(); http.end(); return false; }
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) { _lastHttpEndMs = millis(); http.end(); return false; }
    File file = LittleFS.open(localPath, "w");
    if (!file) { _lastHttpEndMs = millis(); http.end(); return false; }
    uint8_t buf[512]; size_t total = 0;
    int contentLen = http.getSize();
    unsigned long t0 = millis();
    while (http.connected() && (contentLen < 0 || (int)total < contentLen)) {
        size_t avail = stream->available();
        if (avail == 0) { if (millis()-t0>10000) break; delay(1); continue; }
        size_t rd = stream->readBytes(buf, min(avail, sizeof(buf)));
        file.write(buf, rd); total += rd; t0 = millis();
        _notifyActivity();                      // ← folyamatos jelzés letöltés közben
    }
    file.close(); _lastHttpEndMs = millis(); http.end();
    Serial.printf("[BACKEND] downloadFile: %s (%d bytes)\n", localPath.c_str(), (int)total);
    return total > 0;
}

// ── sendBeacon ────────────────────────────────────────────────────────────────
bool BackendClient::sendBeacon(uint8_t volume, bool muted,
                                const String& firmwareVersion,
                                const JsonDocument& statusPayload) {
    JsonDocument req;
    req["volume"] = volume; req["muted"] = muted;
    req["firmwareVersion"] = firmwareVersion;
    req["statusPayload"].set(statusPayload.as<JsonVariantConst>());
    JsonDocument resp; int code = 0;
    return postJson("/devices/beacon", req, resp, code);
}

// ── poll ──────────────────────────────────────────────────────────────────────
bool BackendClient::poll(PolledCommand& outCmd) {
    JsonDocument req;
    JsonDocument resp; int code = 0;
    bool ok = postJson("/devices/poll", req, resp, code);
    if (!ok || code != 200) return false;
    if (!resp["command"].is<JsonObject>()) return false;
    outCmd.id = resp["command"]["id"] | "";
    outCmd.payload.set(resp["command"]["payload"].as<JsonVariantConst>());
    outCmd.hasCommand = outCmd.id.length() > 0;
    return outCmd.hasCommand;
}

// ── ack ───────────────────────────────────────────────────────────────────────
bool BackendClient::ack(const String& commandId, bool ok, const String& errorMsg) {
    JsonDocument req;
    req["commandId"] = commandId; req["ok"] = ok;
    if (errorMsg.length() > 0) req["error"] = errorMsg;
    JsonDocument resp; int code = 0;
    return postJson("/devices/ack", req, resp, code);
}

// ── postJsonUnauthed ──────────────────────────────────────────────────────────
bool BackendClient::postJsonUnauthed(const String& path, const JsonDocument& req,
                                      JsonDocument& resp, int& httpCode) {
    if (_baseUrl.length() == 0) return false;
    waitCooldown();
    _notifyActivity();                          // ← UI aktivitás jelzés
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.setTimeout(7000);
    const String url = _baseUrl + path;
    if (!http.begin(client, url)) { _lastHttpEndMs = millis(); return false; }
    http.addHeader("Content-Type", "application/json");
    String body; serializeJson(req, body);
    httpCode = http.POST(body);
    if (httpCode <= 0) { _lastHttpEndMs = millis(); http.end(); return false; }
    String responseStr = http.getString();
    _lastHttpEndMs = millis(); http.end();
    DeserializationError err = deserializeJson(resp, responseStr);
    if (err) return false;
    return true;
}

// ── confirmProvisioning ───────────────────────────────────────────────────────
bool BackendClient::confirmProvisioning(const String& provisioningToken,
                                         String& outDeviceKey,
                                         String& outWifiSsid,
                                         String& outWifiPass) {
    JsonDocument req; req["provisioningToken"] = provisioningToken;
    JsonDocument resp; int code = 0;
    bool ok = postJsonUnauthed("/provision/provision/confirm", req, resp, code);
    if (!ok || code != 200) return false;
    if (resp["deviceKey"].is<const char*>()) outDeviceKey = resp["deviceKey"].as<String>();
    if (resp["wifi"]["ssid"].is<const char*>()) outWifiSsid = resp["wifi"]["ssid"].as<String>();
    if (resp["wifi"]["password"].is<const char*>()) outWifiPass = resp["wifi"]["password"].as<String>();
    return outDeviceKey.length() > 0;
}