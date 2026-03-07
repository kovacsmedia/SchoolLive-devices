#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <Arduino.h>
#include "Config.h"

class Audio;
class PersistStore;

// Lejátszás utáni TCP/SSL cooldown idő (ms)
#define AUDIO_EOF_COOLDOWN_MS 4000

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

    void notifyEof();
    bool isInCooldown() const;

private:
    Audio*        audio         = nullptr;
    PersistStore* _store        = nullptr;
    uint8_t       currentVolume = 9;   // default, begin()-ben felülírja a store
    bool          _streamMode   = false;
    bool          _eofReceived  = false;
    unsigned long _eofTimeMs    = 0;
};

#endif