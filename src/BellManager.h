#ifndef BELLMANAGER_H
#define BELLMANAGER_H

#include <Arduino.h>
#include "AudioManager.h"
#include "NetworkManager.h"

// Csengetési módok
#define BELL_MODE_OFF 0
#define BELL_MODE_ON 1
#define BELL_MODE_TODAY 2

class BellManager {
public:
    BellManager(AudioManager &audioMgr, NetworkManager &netMgr);
    void begin();
    void loop();
    
    void setBellMode(uint8_t mode);
    uint8_t getBellMode();
    
    int getSecondsToNextEvent();
    
    // ÚJ: Visszaadja a következő csengetés idejét (pl. "08:45" vagy "--:--")
    String getNextEventTimeStr();

private:
    AudioManager &audio;
    NetworkManager &network;
    
    uint8_t _mode = BELL_MODE_ON; 
    
    int _lastDay = -1;
    int _lastMinute = -1;

    static const uint8_t NUM_BELLS = 16;
    static const uint8_t bellHours[16];
    static const uint8_t bellMinutes[16];
    static const uint8_t NUM_SIGNAL_BELLS = 9;
    static const uint8_t signalHours[9];
    static const uint8_t signalMinutes[9];

    void checkSchedule();
};

#endif