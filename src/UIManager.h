#ifndef UIMANAGER_H
#define UIMANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <vector>
#include "Config.h"
#include "AudioManager.h"
#include "SLNetworkManager.h"
#include "BellManager.h"
#include "DeviceTelemetry.h"
#include "PersistStore.h"

class DeviceAgent;
class SnapcastClient;

enum UIState { STATE_NORMAL, STATE_MENU, STATE_NETRADIO, STATE_PROVISIONING };
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
    UIManager(AudioManager &audioMgr, SLNetworkManager &netMgr, BellManager &bellMgr, PersistStore &storeRef);
    void begin();
    void loop();
    void setTelemetry(DeviceTelemetry* tel);

    // Új top-bar állapotjelzőkhöz: a UI olvassa a Snapcast kapcsolat
    // állapotát (S kör) és a DeviceAgent aktív lejátszás akcióját
    // (MESSAGE/RADIO/SIGNAL villogás a bell-time helyett).
    void setAgent(DeviceAgent* agent);
    void setSnapClient(SnapcastClient* snap);

    void drawBootStatus(String status, String details);
    void enterProvisioningMode();
    void updateProvisioningDisplay(const String& mac, const String& ip, const String& status);

    // Remote hangerőállításkor is mutassa a volume-képernyőt (mint gombnyomáskor)
    void showVolumeScreen();

private:
    AudioManager &audio;
    SLNetworkManager &network;
    BellManager &bell;
    PersistStore &_store;
    Adafruit_SSD1306 display;

    UIState uiState = STATE_NORMAL;
    MenuPage menuPage = MENU_MAIN;
    Settings settings;
    DeviceTelemetry* _tel = nullptr;
    DeviceAgent* _agent = nullptr;
    SnapcastClient* _snap = nullptr;

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

    uint8_t _factoryResetConfirmStep = 0;
    unsigned long _factoryResetConfirmTime = 0;

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

    // Új top-bar pictogram helperek:
    // - drawWifiIcon: mobiltelefon-stílusú ))) piktogram a térerő alapján (0-3 ív)
    // - drawConnIcon: 'S' / 'N' betű + kör (üres = inaktív, kitöltött = aktív)
    void drawWifiIcon(int16_t x, int16_t y, int32_t rssi);
    void drawConnIcon(int16_t x, int16_t y, char letter, bool active);

    // Aktív lejátszás akció szöveg a bell-time helyén (TTS→MESSAGE, RADIO→RADIO,
    // BELL→SIGNAL). NULL/üres = nincs aktív lejátszás, a bell time látszik.
    const char* getPlaybackLabel() const;

    void applyDimming();
};

#endif