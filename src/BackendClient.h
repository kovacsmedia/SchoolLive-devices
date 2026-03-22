#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BackendClient.h – HTTPS kommunikáció a SchoolLive backenddel
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#define HTTP_COOLDOWN_MS 200

struct PolledCommand {
    String       id;
    JsonDocument payload;
    bool         hasCommand = false;
};

class BackendClient {
public:
    BackendClient() {}

    void     begin(const String& baseUrl);
    void     setDeviceKey(const String& deviceKey);
    bool     isReady() const;

    bool     postJson(const String& path, const JsonDocument& req,
                      JsonDocument& resp, int& httpCode);
    bool     getJson(const String& path, JsonDocument& resp, int& httpCode);
    bool     postJsonUnauthed(const String& path, const JsonDocument& req,
                               JsonDocument& resp, int& httpCode);

    bool     sendBeacon(uint8_t volume, bool muted,
                        const String& firmwareVersion,
                        const JsonDocument& statusPayload);
    bool     poll(PolledCommand& outCmd);
    bool     ack(const String& commandId, bool ok, const String& errorMsg);
    bool     downloadFile(const String& url, const String& localPath,
                          size_t expectedBytes = 0);
    bool     confirmProvisioning(const String& provisioningToken,
                                  String& outDeviceKey,
                                  String& outWifiSsid,
                                  String& outWifiPass);

    // Snapcast port lekérdezés – tenant-specifikus (1800-1880), 0 = hiba
    uint16_t fetchSnapPort();

private:
    String        _baseUrl;
    String        _deviceKey;
    unsigned long _lastHttpEndMs = 0;

    void addCommonHeaders(HTTPClient& http);
    void waitCooldown();
};