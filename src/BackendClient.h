// BackendClient.h – SchoolLive S3.54
// Változások S3.52 → S3.54:
//   • setUIManager(UIManager*) – opcionális UI pointer regisztráció
//   • postJson / getJson / downloadFile előtt setNetActivity() hívás

#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Forward deklaráció – nem kell teljes UIManager.h include (körkörös include elkerülése)
class UIManager;

struct PolledCommand {
    bool        hasCommand = false;
    String      id         = "";
    JsonDocument payload;
};

class BackendClient {
public:
    void begin(const String& baseUrl);
    void setDeviceKey(const String& deviceKey);
    bool isReady() const;

    /**
     * Opcionális UIManager regisztráció.
     * Ha be van állítva, minden kimenő HTTP kérés előtt setNetActivity()-t hív.
     */
    void setUIManager(UIManager* ui) { _ui = ui; }

    // ── API hívások ────────────────────────────────────────────────────────
    uint16_t fetchSnapPort();
    bool     sendBeacon(uint8_t volume, bool muted,
                        const String& firmwareVersion,
                        const JsonDocument& statusPayload);
    bool     poll(PolledCommand& outCmd);
    bool     ack(const String& commandId, bool ok, const String& errorMsg = "");
    bool     downloadFile(const String& url, const String& localPath,
                          size_t expectedBytes = 0);

    bool     confirmProvisioning(const String& provisioningToken,
                                  String& outDeviceKey,
                                  String& outWifiSsid,
                                  String& outWifiPass);

    // ── Alap HTTP metódusok ────────────────────────────────────────────────
    bool postJson(const String& path, const JsonDocument& req,
                  JsonDocument& resp, int& httpCode);
    bool getJson(const String& path, JsonDocument& resp, int& httpCode);
    bool postJsonUnauthed(const String& path, const JsonDocument& req,
                          JsonDocument& resp, int& httpCode);

private:
    String        _baseUrl   = "";
    String        _deviceKey = "";
    unsigned long _lastHttpEndMs = 0;
    UIManager*    _ui        = nullptr;

    static constexpr unsigned long HTTP_COOLDOWN_MS = 100;

    void     addCommonHeaders(HTTPClient& http);
    void     waitCooldown();
    void     _notifyActivity();   // _ui->setNetActivity() ha _ui != nullptr
};