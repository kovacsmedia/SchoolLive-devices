#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

struct PolledCommand {
    bool        hasCommand = false;
    String      id;
    JsonDocument payload;
};

class BackendClient {
public:
    void begin(const String& baseUrl);
    void setDeviceKey(const String& deviceKey);
    bool isReady() const;

    bool sendBeacon(uint8_t volume, bool muted, const String& firmwareVersion,
                    const JsonDocument& statusPayload);
    bool poll(PolledCommand& outCmd);
    bool ack(const String& commandId, bool ok, const String& errorMsg);

    // GET kérés JSON válasszal
    bool getJson(const String& path, JsonDocument& resp, int& httpCode);

    // Hangfájl letöltése LittleFS-re
    // url: teljes URL (pl. https://api.../audio/bells/hang.mp3)
    // localPath: LittleFS elérési út (pl. /hang.mp3)
    // expectedBytes: ha > 0, méret-ellenőrzés; 0 = skip
    // Visszatér true ha sikeres vagy már létezik megfelelő méretű fájl
    bool downloadFile(const String& url, const String& localPath,
                      size_t expectedBytes = 0);

    // Provisioning (deviceKey nélkül)
    bool confirmProvisioning(const String& provisioningToken,
                             String& outDeviceKey,
                             String& outWifiSsid,
                             String& outWifiPass);

private:
    unsigned long _lastHttpEndMs = 0;
    static const unsigned long HTTP_COOLDOWN_MS = 2500UL;
    String _baseUrl;
    String _deviceKey;

    void waitCooldown();
    bool postJson(const String& path, const JsonDocument& req,
                  JsonDocument& resp, int& httpCode);
    bool postJsonUnauthed(const String& path, const JsonDocument& req,
                          JsonDocument& resp, int& httpCode);
    void addCommonHeaders(HTTPClient& http);
};