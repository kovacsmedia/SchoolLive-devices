#include "BackendClient.h"

void BackendClient::begin(const String& baseUrl) {
  _baseUrl = baseUrl;

  // trailing slash eltávolítás
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

bool BackendClient::postJson(const String& path,
                             const JsonDocument& req,
                             JsonDocument& resp,
                             int& httpCode) {
  if (!isReady()) return false;

  WiFiClientSecure client;
  client.setInsecure();   // később CA/pinning javasolt

  HTTPClient http;
  http.setTimeout(5000);

  const String url = _baseUrl + path;

  if (!http.begin(client, url)) {
    return false;
  }

  addCommonHeaders(http);

  String body;
  serializeJson(req, body);

  httpCode = http.POST(body);

  if (httpCode <= 0) {
    http.end();
    return false;
  }

  String responseStr = http.getString();
  http.end();

  DeserializationError err = deserializeJson(resp, responseStr);
  if (err) {
    return false;
  }

  return (httpCode >= 200 && httpCode < 300);
}

bool BackendClient::sendBeacon(uint8_t volume,
                               bool muted,
                               const String& firmwareVersion,
                               const JsonDocument& statusPayload) {
  JsonDocument req;
  req["volume"] = volume;
  req["muted"] = muted;
  req["firmwareVersion"] = firmwareVersion;

  // ArduinoJson 7 kompatibilis const másolás
  req["statusPayload"].set(statusPayload.as<JsonVariantConst>());

  JsonDocument resp;
  int code = 0;

  return postJson("/devices/beacon", req, resp, code);
}

bool BackendClient::poll(PolledCommand& outCmd) {
  outCmd = PolledCommand{};

  JsonDocument req;
  req["ping"] = (uint32_t)millis();

  JsonDocument resp;
  int code = 0;

  bool ok = postJson("/devices/poll", req, resp, code);
  if (!ok) return false;

  // backend válasz:
  // { ok: true, command: null | { id, payload } }
  if (!resp["ok"].is<bool>() || !resp["ok"].as<bool>()) {
    return false;
  }

  if (resp["command"].isNull()) {
    outCmd.hasCommand = false;
    return true;
  }

  JsonObject cmd = resp["command"].as<JsonObject>();

  outCmd.hasCommand = true;
  outCmd.id = cmd["id"].as<String>();

  // payload teljes másolása
  outCmd.payload.clear();
  outCmd.payload.set(cmd["payload"].as<JsonVariantConst>());

  return true;
}

bool BackendClient::ack(const String& commandId,
                        bool ok,
                        const String& errorMsg) {
  JsonDocument req;
  req["commandId"] = commandId;
  req["ok"] = ok;

  if (!ok) {
    req["error"] = errorMsg;
  }

  JsonDocument resp;
  int code = 0;

  return postJson("/devices/ack", req, resp, code);
}