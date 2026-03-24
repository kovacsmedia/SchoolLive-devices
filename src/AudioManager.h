#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AudioManager.h – ESP32-S3-N16R8
// I2S arbitráció: suspend() / resume() – SnapcastClient veszi át az I2S-t
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include "Config.h"

class Audio;
class PersistStore;

#define AUDIO_EOF_COOLDOWN_MS  3000
#define URL_START_TIMEOUT_MS  25000

class AudioManager {
public:
    AudioManager();
    void begin(PersistStore* store = nullptr);
    void loop();

    void    setVolume(uint8_t vol);
    uint8_t getVolume() const;
    bool    isMuted()   const { return false; }

    // ── Lejátszás ─────────────────────────────────────────────────────────
    void playFile(const char* filename);
    void playUrl(const char* url);
    void playPsram(const uint8_t* buf, size_t len);
    void stop();

    // ── I2S arbitráció (SnapcastClient hívja) ─────────────────────────────
    // suspend(): leállítja az Audio objektumot és felszabadítja az I2S drivert
    // resume():  újraindítja az Audio objektumot (Snap után)
    void suspend();
    void resume();
    bool isSuspended() const { return _suspended; }

    // ── Állapot ───────────────────────────────────────────────────────────
    bool isPlaying()    const;
    bool isStreamMode() const;
    bool isBusy()       const { return _urlActive; }
    bool isInCooldown() const;

    void notifyEof();
    void notifyError();

private:
    Audio*        audio         = nullptr;
    PersistStore* _store        = nullptr;
    uint8_t       currentVolume = 9;
    bool          _suspended    = false;

    bool          _streamMode   = false;
    bool          _eofReceived  = false;
    unsigned long _eofTimeMs    = 0;
    bool          _urlActive    = false;
    unsigned long _urlStartMs   = 0;
    bool          _urlHasPlayed = false;

    static const char* PSRAM_TEMP_PATH;

    void _initAudio();
    void _deinitAudio();
};