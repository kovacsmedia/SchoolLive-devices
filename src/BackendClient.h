#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

struct PolledCommand {
  bool hasCommand = false;
  String id;
  JsonDocument payload;   // teljes payload objektum
};

class BackendClient {
public:
  void begin(const String& baseUrl);
  void setDeviceKey(const String& deviceKey);
  bool isReady() const;

  bool sendBeacon(uint8_t volume, bool muted, const String& firmwareVersion, const JsonDocument& statusPayload);
  bool poll(PolledCommand& outCmd);
  bool ack(const String& commandId, bool ok, const String& errorMsg);

private:
  String _baseUrl;
  String _deviceKey;

  bool postJson(const String& path, const JsonDocument& req, JsonDocument& resp, int& httpCode);
  void addCommonHeaders(HTTPClient& http);
};