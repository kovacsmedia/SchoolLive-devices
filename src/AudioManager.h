#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <Arduino.h>
#include "Config.h"

class Audio;
class PersistStore;

#define AUDIO_EOF_COOLDOWN_MS 10000
#define URL_START_TIMEOUT_MS 25000

class AudioManager {
public:
    AudioManager();

    void begin(PersistStore* store = nullptr);
    void loop();

    void setVolume(uint8_t vol);
    uint8_t getVolume() const;

    bool isMuted() const {
        return false;
    }

    void playFile(const char* filename);
    void playUrl(const char* url);

    void stop();

    bool isPlaying() const;
    bool isStreamMode() const;

    bool isBusy() const {
        return _urlActive || _localFileActive;
    }

    void notifyEof();
    void notifyError();

    bool isInCooldown() const;

    // I2S arbitration hookok.
    // Helyi/offline csengetés előtt a main leállítja a Snapcast I2S-t,
    // EOF/hiba/stop után pedig visszaengedi.
    void setI2SCallbacks(
        void (*beforeLocalPlayback)(),
        void (*afterLocalPlayback)()
    );

private:
    Audio* audio = nullptr;
    PersistStore* _store = nullptr;

    uint8_t currentVolume = 9;

    bool _streamMode = false;
    bool _eofReceived = false;
    unsigned long _eofTimeMs = 0;

    bool _urlActive = false;
    unsigned long _urlStartMs = 0;
    bool _urlHasPlayed = false;

    bool _localFileActive = false;

    void (*_beforeLocalPlayback)() = nullptr;
    void (*_afterLocalPlayback)() = nullptr;

    void ensureAudio();
    void releaseLocalPlaybackIfNeeded();
};

#endif