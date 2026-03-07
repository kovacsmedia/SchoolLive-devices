#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <Arduino.h>
#include "Config.h"

class Audio;

// Lejátszás utáni TCP/SSL cooldown idő (ms)
// Ez alatt a DeviceAgent nem nyit új HTTP kapcsolatot
#define AUDIO_EOF_COOLDOWN_MS 4000

class AudioManager {
public:
    AudioManager();
    void begin();
    void loop();

    void setVolume(uint8_t vol);
    uint8_t getVolume() const;
    bool isMuted() const { return false; }

    void playFile(const char* filename);
    void playUrl(const char* url);
    void stop();

    bool isPlaying() const;
    bool isStreamMode() const;

    // EOF callback hívja meg
    void notifyEof();

    // DeviceAgent és BellManager ezt kérdezze le HTTP előtt
    bool isInCooldown() const;

private:
    Audio*   audio         = nullptr;
    uint8_t  currentVolume = 10;
    bool     _streamMode   = false;
    bool     _eofReceived  = false;

    unsigned long _eofTimeMs = 0;  // mikor volt az utolsó EOF
};

#endif