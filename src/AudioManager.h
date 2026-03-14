#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AudioManager.h – ESP32-S3-N16R8 verzió
// Újítások:
//   • playPsram() – PSRAM-ból közvetlen lejátszás (TTS pre-fetch)
//   • PSRAM stream buffer (64KB) az audio library-nek
//   • Eltávolítva: SSL cooldown (már nem blokkolja a hálózatot)
//   • Javított watchdog logika
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include "Config.h"

class Audio;
class PersistStore;

#define AUDIO_EOF_COOLDOWN_MS  3000   // S3-n gyorsabb TCP cleanup
#define URL_START_TIMEOUT_MS  25000

class AudioManager {
public:
    AudioManager();
    void begin(PersistStore* store = nullptr);
    void loop();

    void    setVolume(uint8_t vol);
    uint8_t getVolume() const;
    bool    isMuted() const { return false; }

    // ── Lejátszás ─────────────────────────────────────────────────────────
    void playFile(const char* filename);          // LittleFS
    void playUrl(const char* url);               // HTTP/S stream
    void playPsram(const uint8_t* buf, size_t len);  // PSRAM buffer (TTS)
    void stop();

    // ── Állapot ───────────────────────────────────────────────────────────
    bool isPlaying()    const;
    bool isStreamMode() const;
    bool isBusy()       const { return _urlActive; }
    bool isInCooldown() const;

    // ── Callbacks (audio library-ből) ─────────────────────────────────────
    void notifyEof();
    void notifyError();

private:
    Audio*        audio         = nullptr;
    PersistStore* _store        = nullptr;
    uint8_t       currentVolume = 9;

    bool          _streamMode   = false;
    bool          _eofReceived  = false;
    unsigned long _eofTimeMs    = 0;
    bool          _urlActive    = false;
    unsigned long _urlStartMs   = 0;
    bool          _urlHasPlayed = false;

    // PSRAM alapú temp fájl a playPsram()-hoz
    static const char* PSRAM_TEMP_PATH;
};