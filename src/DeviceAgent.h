#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "DeviceTelemetry.h"
#include "AudioManager.h"
#include "SLNetworkManager.h"
#include "BackendClient.h"
#include "BellManager.h"
#include "UIManager.h"
#include "WsClient.h"
#include "SnapcastClient.h"
#include "PersistStore.h"
#include "OtaManager.h"

class DeviceAgent {
public:
    void begin(
        SLNetworkManager& net,
        AudioManager& audio,
        UIManager& ui,
        BackendClient& backend,
        DeviceTelemetry& tel,
        WsClient& ws,
        BellManager& bells,
        SnapcastClient& snap,
        PersistStore& store,
        OtaManager& ota
    );

    void loop();

    // Bejövő WS üzenet feldolgozása – a WsClient onMessage callbackja hívja
    void onWsMessage(const JsonDocument& msg);

    void setFirmwareVersion(const String& v) { _fw = v; }

    bool isPlaybackQuietActive() const;
    unsigned long playbackQuietRemainingMs() const;

    const String& getCurrentPlaybackAction() const { return _currentPlaybackAction; }

private:
    SLNetworkManager* _net      = nullptr;
    AudioManager*     _audio    = nullptr;
    UIManager*        _ui       = nullptr;
    BackendClient*    _backend  = nullptr;
    DeviceTelemetry*  _tel      = nullptr;
    WsClient*         _ws       = nullptr;
    BellManager*      _bells    = nullptr;
    SnapcastClient*   _snap     = nullptr;
    PersistStore*     _store    = nullptr;
    OtaManager*       _ota      = nullptr;

    String _fw            = "dev";
    String _deviceId;

    unsigned long _lastBeaconMs   = 0;
    const unsigned long BEACON_INTERVAL_MS = 30000UL;

    unsigned long _playbackQuietUntilMs = 0;
    String        _currentPlaybackAction;

    bool _emergencyOverrideActive = false;

    const unsigned long PLAYBACK_SAFETY_MS      = 10000UL;
    const unsigned long DEFAULT_PLAYBACK_QUIET_MS = 60000UL;

    // ── WS üzenet kezelők ─────────────────────────────────────────────────
    void handleHello(const JsonDocument& msg);
    void handlePrepare(const JsonDocument& msg);
    void handlePlay(const JsonDocument& msg);
    void handleCommand(const JsonDocument& msg);
    void handleBeaconAck(const JsonDocument& msg);

    // ── Parancs végrehajtás ────────────────────────────────────────────────
    bool executeCommand(const String& commandId, JsonVariantConst payload);

    bool handleSetVolume(JsonVariantConst payload, String& err);
    bool handleSetChannelMode(JsonVariantConst payload, String& err);
    bool handleShowMessage(JsonVariantConst payload, String& err);

    String detectAction(JsonVariantConst payload) const;
    bool   looksLikeAudioCommand(JsonVariantConst payload, const String& action) const;

    unsigned long getPlaybackQuietMs(JsonVariantConst payload) const;
    void          enterPlaybackQuiet(JsonVariantConst payload, const String& action);
    void          maybeClearEmergencyOverride();

    // ── Beacon küldés WS-en ───────────────────────────────────────────────
    void sendBeaconIfDue();
};
