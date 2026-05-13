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

// A backend /firmware/check válasza, ha új firmware érhető el.
struct FirmwareCheckResult {
    bool   updateAvailable = false;
    String version;        // pl. "S4.5"
    String url;            // .bin URL
    int    sizeBytes  = 0;
    String sha256;         // hexa string, lehet üres ha nincs sha
    bool   mandatory  = false;
    String notes;
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

    // ── OTA (firmware update) ──────────────────────────────────────────────
    //
    // GET /firmware/check?version=<curr>&deviceClass=SPEAKER&hwModel=ESP32_S3
    // Header: x-device-key
    // Válasz: { updateAvailable, latest: { version, url, sizeBytes, sha256, mandatory, notes } }
    bool checkFirmware(
        const String& currentVersion,
        const String& deviceClass,
        const String& hwModel,
        FirmwareCheckResult& outResult
    );

    // POST /firmware/ota-status
    // Header: x-device-key
    // Body: { version, status: "DOWNLOADING"|"INSTALLING"|"SUCCESS"|"FAILED"|"ROLLBACK", progress?, error? }
    bool reportOtaStatus(
        const String& version,
        const String& status,
        int progress,
        const String& errorMsg
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