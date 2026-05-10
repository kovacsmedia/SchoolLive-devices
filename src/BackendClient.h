#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>

struct PolledCommand {
    bool hasCommand = false;
    String id;
    JsonDocument payload;
};

class BackendClient {
public:
    void begin(const String& baseUrl);
    void setDeviceKey(const String& deviceKey);

    bool isReady() const;

    bool sendBeacon(
        uint8_t volume,
        bool muted,
        const String& firmwareVersion,
        const JsonDocument& statusPayload
    );

    bool poll(PolledCommand& outCmd);
    bool ack(const String& commandId, bool ok, const String& errorMsg);

    // GET kérés JSON válasszal
    bool getJson(const String& path, JsonDocument& resp, int& httpCode);

    // Hangfájl letöltése LittleFS-re
    bool downloadFile(
        const String& url,
        const String& localPath,
        size_t expectedBytes = 0
    );

    // Provisioning
    bool confirmProvisioning(
        const String& provisioningToken,
        String& outDeviceKey,
        String& outWifiSsid,
        String& outWifiPass
    );

    // ── Snapcast konfiguráció a backend beacon válaszából ────────────────

    bool hasSnapConfig() const {
        return _snapConfigValid;
    }

    String getDeviceId() const {
        return _deviceId;
    }

    String getSnapHost() const {
        return _snapHost;
    }

    uint16_t getSnapPort() const {
        return _snapPort;
    }

private:
    unsigned long _lastHttpEndMs = 0;
    static const unsigned long HTTP_COOLDOWN_MS = 2500UL;

    String _baseUrl;
    String _deviceKey;

    String _deviceId;
    String _snapHost;
    uint16_t _snapPort = 0;
    bool _snapConfigValid = false;

    void waitCooldown();

    bool postJson(
        const String& path,
        const JsonDocument& req,
        JsonDocument& resp,
        int& httpCode
    );

    bool postJsonUnauthed(
        const String& path,
        const JsonDocument& req,
        JsonDocument& resp,
        int& httpCode
    );

    void addCommonHeaders(HTTPClient& http);
    void parseBeaconResponse(const JsonDocument& resp);
};