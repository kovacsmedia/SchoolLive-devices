#include "BellManager.h"

const uint8_t BellManager::bellHours[16] = {8, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15, 15, 16};
const uint8_t BellManager::bellMinutes[16] = {0, 45, 55, 40, 55, 40, 50, 35, 55, 40, 50, 35, 15, 0, 15, 0};
const uint8_t BellManager::signalHours[9] = {7, 7, 8, 9, 10, 11, 12, 14, 15};
const uint8_t BellManager::signalMinutes[9] = {30, 55, 53, 53, 48, 53, 48, 10, 13};

BellManager::BellManager(AudioManager &audioMgr, NetworkManager &netMgr) 
    : audio(audioMgr), network(netMgr) {}

void BellManager::begin() {}

void BellManager::loop() {
    if (_mode != BELL_MODE_OFF && network.isTimeSynced()) {
        checkSchedule();
    }
}

void BellManager::checkSchedule() {
    struct tm t = network.getTimeInfo();
    
    if (_mode == BELL_MODE_ON) {
        if (t.tm_wday == 0 || t.tm_wday == 6) return;
    }

    int curMin = t.tm_hour * 60 + t.tm_min;
    if (_lastDay == t.tm_yday && _lastMinute == curMin) return;

    bool ringing = false;

    for (int i = 0; i < NUM_SIGNAL_BELLS; i++) {
        if (signalHours[i] == t.tm_hour && signalMinutes[i] == t.tm_min) {
            audio.playFile("/jelzocsengo.mp3"); ringing = true; break;
        }
    }
    if (!ringing) {
        for (int i = 0; i < NUM_BELLS; i++) {
            if (bellHours[i] == t.tm_hour && bellMinutes[i] == t.tm_min) {
                audio.playFile("/kibecsengo.mp3"); ringing = true; break;
            }
        }
    }

    if (ringing) { 
        _lastDay = t.tm_yday; 
        _lastMinute = curMin; 
        
        if (_mode == BELL_MODE_TODAY) {
            if (t.tm_hour == 16 && t.tm_min == 0) {
                _mode = BELL_MODE_ON; 
            }
        }
    }
}

void BellManager::setBellMode(uint8_t mode) { 
    if (mode > 2) mode = 0;
    _mode = mode; 
}

uint8_t BellManager::getBellMode() { return _mode; }

int BellManager::getSecondsToNextEvent() {
    if (!network.isTimeSynced()) return -1;
    struct tm t = network.getTimeInfo();
    
    if (_mode == BELL_MODE_OFF) return -1;
    if (_mode == BELL_MODE_ON && (t.tm_wday == 0 || t.tm_wday == 6)) return -1;
    
    int curSec = t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    int minDiff = 999999;
    for (int i = 0; i < NUM_BELLS; i++) {
        int diff = (bellHours[i]*3600+bellMinutes[i]*60) - curSec;
        if (diff > 0 && diff < minDiff) minDiff = diff;
    }
    for (int i = 0; i < NUM_SIGNAL_BELLS; i++) {
        int diff = (signalHours[i]*3600+signalMinutes[i]*60) - curSec;
        if (diff > 0 && diff < minDiff) minDiff = diff;
    }
    return (minDiff == 999999) ? -1 : minDiff;
}

// ÚJ FÜGGVÉNY: Következő csengetés ideje (HH:MM)
String BellManager::getNextEventTimeStr() {
    if (!network.isTimeSynced()) return "--:--";
    struct tm t = network.getTimeInfo();

    if (_mode == BELL_MODE_OFF) return "OFF";
    if (_mode == BELL_MODE_ON && (t.tm_wday == 0 || t.tm_wday == 6)) return "--:--";

    int curMinuteOfDay = t.tm_hour * 60 + t.tm_min;
    int nextEventMin = 9999;
    
    // Keressük a legközelebbi időpontot (percekben)
    for (int i = 0; i < NUM_BELLS; i++) {
        int eventMin = bellHours[i] * 60 + bellMinutes[i];
        if (eventMin > curMinuteOfDay && eventMin < nextEventMin) nextEventMin = eventMin;
    }
    for (int i = 0; i < NUM_SIGNAL_BELLS; i++) {
        int eventMin = signalHours[i] * 60 + signalMinutes[i];
        if (eventMin > curMinuteOfDay && eventMin < nextEventMin) nextEventMin = eventMin;
    }

    if (nextEventMin == 9999) return "--:--"; // Nincs több ma

    int h = nextEventMin / 60;
    int m = nextEventMin % 60;
    
    char buf[6];
    sprintf(buf, "%02d:%02d", h, m);
    return String(buf);
}