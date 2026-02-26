#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <Arduino.h>
#include "Config.h"

// Forward declaration, hogy a header ne húzza be a nagy könyvtárat
class Audio;

class AudioManager {
public:
    AudioManager();
    void begin();
    void loop();

    void setVolume(uint8_t vol);
    uint8_t getVolume() const;

    // Offline bell: helyi fájl (LittleFS)
    void playFile(const char* filename);

    // Online: URL-ről üzenet / streaming
    void playUrl(const char* url);

    void stop();
    bool isPlaying() const;

    // true, ha URL/stream jellegű lejátszásban vagyunk
    bool isStreamMode() const;

private:
    Audio* audio = nullptr;      // pointer → a nagy Audio.h csak .cpp-ben lesz include-olva
    uint8_t currentVolume = 5;
    bool _streamMode = false;
};

#endif