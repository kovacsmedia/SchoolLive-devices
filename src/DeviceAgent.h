// DeviceAgent.h – SchoolLive S3.54
// Változások:
//   • _firstBeacon bool – azonnali első beacon küldéshez
//   • _sendBeacon() privát segédfüggvény

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "DeviceTelemetry.h"
#include "AudioManager.h"
#include "NetworkManager.h"
#include "BackendClient.h"
#include "UIManager.h"

class DeviceAgent {
public:
    DeviceTelemetry* _tel = nullptr;

    void begin(NetworkManager& net, AudioManager& audio,
               UIManager& ui, BackendClient& backend, DeviceTelemetry& tel);
    void setFirmwareVersion(const String& v) { _fw = v; }
    void loop();

    bool executeCommand(const String& commandId, const String& action,
                        const String& url, const String& text,
                        const String& title);

private:
    NetworkManager* _net     = nullptr;
    AudioManager*   _audio   = nullptr;
    UIManager*      _ui      = nullptr;
    BackendClient*  _backend = nullptr;
    String          _fw      = "S3.54";

    unsigned long _lastBeaconMs = 0;
    unsigned long _lastPollMs   = 0;
    bool          _firstBeacon  = true;   // ← első beacon azonnali küldés

    static const unsigned long BEACON_INTERVAL_MS = 30000UL;
    static const unsigned long POLL_INTERVAL_MS   = 5000UL;

    void sendBeaconIfDue();
    void _sendBeacon();           // ← tényleges beacon küldés
    void pollIfDue();
    bool executeAndAck(const PolledCommand& cmd);

    bool handlePlayUrl(JsonVariantConst p, String& err);
    bool handleBell(JsonVariantConst p, String& err);
    bool handleTts(JsonVariantConst p, String& err);
    bool handleStop(String& err);
    bool handleSetVolume(JsonVariantConst p, String& err);
    bool handleShowMessage(JsonVariantConst p, String& err);
};