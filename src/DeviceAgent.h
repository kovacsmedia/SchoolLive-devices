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
    void begin(
        NetworkManager& net,
        AudioManager& audio,
        UIManager& ui,
        BackendClient& backend,
        DeviceTelemetry& tel
    );

    void loop();

    void setFirmwareVersion(const String& v) {
        _fw = v;
    }

    bool isPlaybackQuietActive() const;
    unsigned long playbackQuietRemainingMs() const;

private:
    NetworkManager* _net = nullptr;
    AudioManager* _audio = nullptr;
    UIManager* _ui = nullptr;
    BackendClient* _backend = nullptr;
    DeviceTelemetry* _tel = nullptr;

    String _fw = "dev";

    unsigned long _lastBeaconMs = 0;
    unsigned long _lastPollMs = 0;

    const unsigned long BEACON_INTERVAL_MS = 30000UL;
    const unsigned long POLL_INTERVAL_MS = 1500UL;

    unsigned long _playbackQuietUntilMs = 0;

    const unsigned long PLAYBACK_SAFETY_MS = 10000UL;
    const unsigned long DEFAULT_PLAYBACK_QUIET_MS = 60000UL;

    void sendBeaconIfDue();
    void pollIfDue();

    bool executeAndAck(const PolledCommand& cmd);

    bool handleSetVolume(JsonVariantConst payload, String& err);
    bool handleShowMessage(JsonVariantConst payload, String& err);

    String detectAction(JsonVariantConst payload) const;
    bool looksLikeAudioCommand(JsonVariantConst payload, const String& action) const;

    unsigned long getPlaybackQuietMs(JsonVariantConst payload) const;
    void enterPlaybackQuiet(JsonVariantConst payload, const String& action);
};