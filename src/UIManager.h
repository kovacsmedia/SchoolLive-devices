#ifndef UIMANAGER_H
#define UIMANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <vector>
#include "Config.h"
#include "AudioManager.h"
#include "NetworkManager.h"
#include "BellManager.h"

enum UIState { STATE_NORMAL, STATE_MENU, STATE_NETRADIO };
enum MenuPage { MENU_MAIN, MENU_SUB };

struct Settings {
    uint8_t bellMode = 1; 
    bool soundEnabled = true;
    uint8_t clockMode = 1; 
    bool countEnabled = false;
    uint8_t dimmLevel = 0; 
};

struct RadioStation {
    String name;
    String url;
};

class UIManager {
public:
    // 3 paraméter (Nincs BT)
    UIManager(AudioManager &audioMgr, NetworkManager &netMgr, BellManager &bellMgr);
    void begin();
    void loop();
    
    void drawBootStatus(String status, String details);

private:
    AudioManager &audio;
    NetworkManager &network;
    BellManager &bell;
    Adafruit_SSD1306 display;
    
    UIState uiState = STATE_NORMAL;
    MenuPage menuPage = MENU_MAIN;
    Settings settings;

    int8_t mainMenuIndex = 0; 
    int8_t subMenuIndex = 0;
    
    std::vector<RadioStation> radioList;
    int currentStationIndex = 0;
    bool isRadioPlaying = false;
    unsigned long streamStalledTime = 0;

    unsigned long lastUiUpdate = 0;
    unsigned long volumeDisplayUntil = 0;
    
    bool btnL_Last = false;
    bool btnR_Last = false;
    unsigned long btnL_PressTime = 0;
    unsigned long btnR_PressTime = 0;
    
    uint8_t pendingClicksL = 0;
    uint8_t pendingClicksR = 0;
    unsigned long lastClickTimeL = 0;
    unsigned long lastClickTimeR = 0;

    void handleButtonL();
    void handleButtonR();
    
    void processClickL(uint8_t clicks);
    void processLongPressL();
    void processClickR(uint8_t clicks);
    void processLongPressR();

    void navigateMenuNext();
    void navigateMenuBack();
    void executeMenuAction();

    void enterNetRadio();
    void exitNetRadio();
    
    void playNextStation();
    void playCurrentStation();
    
    void parseRadioList(String data);
    void checkStreamHealth();

    void actionVolumeUp(bool beep = true); 
    void actionVolumeDown(bool beep = true);
    void playFeedback();

    void updateDisplay();
    void drawClockScreen();
    void drawMenuScreen();
    void drawNetRadioScreen();
    void drawVolumeScreen();
    void drawStatusScreen();
    void drawSplashScreen();
    
    void applyDimming();
};

#endif