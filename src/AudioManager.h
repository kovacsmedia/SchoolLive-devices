#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <Arduino.h>
#include "Config.h"

class Audio;

class AudioManager {
public:
    AudioManager();
    void begin();
    void loop();

    void setVolume(uint8_t vol);
    uint8_t getVolume() const;

    void playFile(const char* filename);
    void playUrl(const char* url);
    void stop();

    bool isPlaying() const;
    bool isStreamMode() const;

    // EOF callback hívja meg
    void notifyEof();

private:
    Audio* audio = nullptr;
    uint8_t currentVolume = 5;
    bool _streamMode = false;
    bool _eofReceived = false;
};

#endif