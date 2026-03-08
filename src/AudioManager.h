#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <Arduino.h>
#include "Config.h"

class Audio;
class PersistStore;

// Lejátszás utáni TCP/SSL cooldown idő (ms)
#define AUDIO_EOF_COOLDOWN_MS 10000

// Watchdog: ha 25s alatt nem indul el a lejátszás → cleanup
#define URL_START_TIMEOUT_MS 25000

class AudioManager {
public:
    AudioManager();
    void begin(PersistStore* store = nullptr);
    void loop();

    void    setVolume(uint8_t vol);
    uint8_t getVolume() const;
    bool    isMuted() const { return false; }

    void playFile(const char* filename);
    void playUrl(const char* url);
    void stop();

    bool isPlaying() const;
    bool isStreamMode() const;

    // true amíg URL stream aktív (playUrl()-tól EOF/stop/hiba-ig)
    bool isBusy() const { return _urlActive; }

    void notifyEof();
    void notifyError();
    bool isInCooldown() const;

private:
    Audio*        audio          = nullptr;
    PersistStore* _store         = nullptr;
    uint8_t       currentVolume  = 9;
    bool          _streamMode    = false;
    bool          _eofReceived   = false;
    unsigned long _eofTimeMs     = 0;
    bool          _urlActive     = false;  // playUrl()-tól EOF/stop/hiba-ig true
    unsigned long _urlStartMs    = 0;      // mikor hívtuk a playUrl()-t
    bool          _urlHasPlayed  = false;  // ténylegesen elindult-e az isRunning()
};

#endif