#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

struct PolledCommand {
    bool         hasCommand = false;
    String       id;
    JsonDocument payload;
};

class BackendClient {
public:
    void begin(const String& baseUrl);
    void setDeviceKey(const String& deviceKey);
    bool isReady() const;

    bool sendBeacon(uint8_t volume, bool muted,
                    const String& firmwareVersion,
                    const JsonDocument& statusPayload);
    bool poll(PolledCommand& outCmd);
    bool ack(const String& commandId, bool ok, const String& errorMsg);
    bool getJson(const String& path, JsonDocument& resp, int& httpCode);
    bool downloadFile(const String& url, const String& localPath,
                      size_t expectedBytes = 0);
    bool confirmProvisioning(const String& provisioningToken,
                             String& outDeviceKey,
                             String& outWifiSsid,
                             String& outWifiPass);

    // Public – OtaManager is használja
    bool postJson(const String& path, const JsonDocument& req,
                  JsonDocument& resp, int& httpCode);

private:
    static const unsigned long HTTP_COOLDOWN_MS = 500UL;
    unsigned long _lastHttpEndMs = 0;
    String _baseUrl;
    String _deviceKey;

    void waitCooldown();
    bool postJsonUnauthed(const String& path, const JsonDocument& req,
                          JsonDocument& resp, int& httpCode);
    void addCommonHeaders(HTTPClient& http);
};