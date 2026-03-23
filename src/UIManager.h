// UIManager.h – SchoolLive S3.54
// Változások S3.52 → S3.54:
//   • setNetActivity() – aktív kommunikáció jelzése az online ikonnak (500ms teli kör)
//   • _netActivityMs  – privát timestamp az aktivitás ablakához
//   • _drawStatusBar(), _drawWifiIcon(), _drawOnlineIcon(), _drawSnapIcon() – piktogram metódusok
//   • Szöveges ONLINE/OFFLINE + RSSI% felső sor eltávolítva
//   • _backendOnline, _snapOnline, _netActivityMs: volatile – Core 0 írja, Core 1 olvassa!
//     volatile nélkül a fordító cackelhet és a Core 1 soha nem látja a frissített értéket.

#ifndef UIMANAGER_H
#define UIMANAGER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include "Config.h"
#include "AudioManager.h"
#include "NetworkManager.h"
#include "BellManager.h"
#include "DeviceTelemetry.h"
#include "PersistStore.h"

enum UIState  { STATE_NORMAL, STATE_MENU, STATE_PROVISIONING };
enum MenuPage { MENU_MAIN, MENU_SUB };

struct Settings {
    uint8_t bellMode     = 1;
    bool    soundEnabled = true;
    uint8_t clockMode    = 1;
    bool    countEnabled = false;
    uint8_t dimmLevel    = 0;
};

class UIManager {
public:
    UIManager(AudioManager& audioMgr, NetworkManager& netMgr,
              BellManager& bellMgr, PersistStore& storeRef);

    void begin();
    void loop();

    // ── Telemetry ──────────────────────────────────────────────────────────
    void setTelemetry(DeviceTelemetry* tel);

    // ── Boot / provisioning ────────────────────────────────────────────────
    void drawBootStatus(String status, String details);
    void enterProvisioningMode();
    void updateProvisioningDisplay(const String& mac, const String& ip,
                                   const String& status);

    // ── Snapcast állapot (Core 0 → Core 1 határon!) ────────────────────────
    void setSnapStatus(bool online, const String& info = "");

    // ── Lejátszás indikátor ────────────────────────────────────────────────
    void setPlayingState(const String& type);

    // ── Backend online státusz (Core 0 → Core 1 határon!) ─────────────────
    void setBackendOnline(bool online);

    // ── Hálózati aktivitás jelzése (Core 0 → Core 1 határon!) ─────────────
    // Hívd minden kimenő HTTP/WS kérés ELŐTT.
    // Az online ikon 500ms-ig teli körként jelenik meg.
    void setNetActivity();

private:
    // ── Hardware ───────────────────────────────────────────────────────────
    AudioManager&    audio;
    NetworkManager&  network;
    BellManager&     bell;
    PersistStore&    _store;
    Adafruit_SSD1306 display;
    DeviceTelemetry* _tel = nullptr;

    // ── UI állapot ─────────────────────────────────────────────────────────
    UIState   uiState   = STATE_NORMAL;
    MenuPage  menuPage  = MENU_MAIN;
    Settings  settings;

    int8_t mainMenuIndex = 0;
    int8_t subMenuIndex  = 0;

    unsigned long lastUiUpdate       = 0;
    unsigned long volumeDisplayUntil = 0;

    // ── Cross-core változók: volatile! ─────────────────────────────────────
    // Core 0 (TaskNetwork, TaskSnapcast) írja → Core 1 (loop) olvassa.
    // volatile nélkül a GCC optimalizáló soha nem olvassa vissza RAM-ból.
    volatile bool          _backendOnline  = false;
    volatile bool          _snapOnline     = false;
    volatile unsigned long _netActivityMs  = 0;

    // _snapInfo String – ritkán változik, race condition elviselhető,
    // csak a STATUS menüben jelenik meg (nem a fő körben)
    String  _snapInfo = "";

    // ── Lejátszás villogó ──────────────────────────────────────────────────
    String        _playingType    = "";
    unsigned long _playingStartMs = 0;
    static const unsigned long PLAYING_FLASH_MS = 5000;

    // ── Factory reset ──────────────────────────────────────────────────────
    uint8_t       _factoryResetConfirmStep = 0;
    unsigned long _factoryResetConfirmTime = 0;

    // ── Gomb: nem-blokkoló state machine ──────────────────────────────────
    enum BtnState { BTN_IDLE, BTN_PRESSED, BTN_WAIT_DOUBLE };

    BtnState      _btnLState     = BTN_IDLE;
    BtnState      _btnRState     = BTN_IDLE;
    unsigned long _btnLPressMs   = 0;
    unsigned long _btnRPressMs   = 0;
    unsigned long _btnLReleaseMs = 0;
    unsigned long _btnRReleaseMs = 0;

    // ── Gomb feldolgozás ───────────────────────────────────────────────────
    void pollButtons();
    void processClickL(uint8_t clicks);
    void processLongPressL();
    void processClickR(uint8_t clicks);
    void processLongPressR();

    // ── Menü logika ────────────────────────────────────────────────────────
    void navigateMenuNext();
    void navigateMenuBack();
    void executeMenuAction();

    // ── Hangerő ────────────────────────────────────────────────────────────
    void actionVolumeUp();
    void actionVolumeDown();

    // ── Kijelző ────────────────────────────────────────────────────────────
    void updateDisplay();
    void drawClockScreen();
    void drawMenuScreen();
    void drawStatusScreen();
    void drawVolumeScreen();
    void drawSplashScreen();
    void applyDimming();

    // ── Státuszsor piktogramok (felső 8px) ────────────────────────────────
    /** WiFi + Online + Snap + MUTE – teljes felső sáv */
    void _drawStatusBar();
    /** 4 bár RSSI alapján, alulra igazítva (11x8px). bars=0: nincs WiFi */
    void _drawWifiIcon(int16_t x, int16_t y, int8_t bars);
    /**
     * (8x8px): offline→üres kör+X | idle→üres kör+pipa | active→teli kör
     */
    void _drawOnlineIcon(int16_t x, int16_t y, bool online, bool active);
    /** (9x8px): connected→hangszóró+hullám | false→hangszóró+slash */
    void _drawSnapIcon(int16_t x, int16_t y, bool connected);
};

#endif