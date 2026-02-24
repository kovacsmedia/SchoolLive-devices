#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <Arduino.h>
#include "Audio.h"
#include <LittleFS.h>
#include "Config.h"

class AudioManager {
public:
    AudioManager();
    void begin();
    void loop();
    
    void setVolume(uint8_t vol);
    uint8_t getVolume();
    
    void playFile(const char* filename);
    void playUrl(const char* url);
    
    void stop();
    bool isPlaying();
    bool isStreamMode();

private:
    Audio audio;
    uint8_t currentVolume = 5;
    bool _streamMode = false;
};

#endif