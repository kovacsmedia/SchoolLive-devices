// UIManager.cpp – SchoolLive S3.52
// Változások S3.52 → S3.53:
//   • Felső sor szöveges státusz → piktogram alapú (_drawStatusBar)
//     - WiFi: 4 bár RSSI alapján (üres körvonal / teli kitöltés)
//     - Online: üres kör+pipa (idle) | teli kör (aktív kommunikáció) | üres kör+X (offline)
//     - Snap: hangszóró+hullám (csatlakozva) | hangszóró+slash (nem csatlakozva)
//   • setNetActivity() – hívd hálózati kérés előtt, 500ms-ig "aktív" a kör
//   • Dimming: 5 fokozat marad (5. fokozat = kontrast 5/255 → SSD1306 minimum)

#include "UIManager.h"

// ── Interrupt (csak jelzés, nincs blokkolás) ──────────────────────────────────
volatile bool _uiBtnL = false;
volatile bool _uiBtnR = false;
volatile unsigned long _uiBtnDebounce = 0;

void IRAM_ATTR uiIsrL() {
    if (millis() - _uiBtnDebounce > 40) { _uiBtnL = true; _uiBtnDebounce = millis(); }
}
void IRAM_ATTR uiIsrR() {
    if (millis() - _uiBtnDebounce > 40) { _uiBtnR = true; _uiBtnDebounce = millis(); }
}

// ── Konstruktor ───────────────────────────────────────────────────────────────
UIManager::UIManager(AudioManager& audioMgr, NetworkManager& netMgr,
                     BellManager& bellMgr, PersistStore& storeRef)
    : audio(audioMgr), network(netMgr), bell(bellMgr), _store(storeRef),
      display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1) {}

// ── begin ─────────────────────────────────────────────────────────────────────
void UIManager::begin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
    delay(100);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        Serial.println("[UI] SSD1306 init FAILED!");
    } else {
        Serial.println("[UI] SSD1306 OK");
    }
    display.setRotation(2);
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);

    applyDimming();
    drawSplashScreen();
    delay(2000);

    pinMode(TOUCH_L, INPUT);
    pinMode(TOUCH_R, INPUT);
    attachInterrupt(digitalPinToInterrupt(TOUCH_L), uiIsrL, RISING);
    attachInterrupt(digitalPinToInterrupt(TOUCH_R), uiIsrR, RISING);

    bell.setBellMode(settings.bellMode);
    Serial.println("[UI] begin kész");
}

// ── loop ──────────────────────────────────────────────────────────────────────
void UIManager::loop() {
    if (uiState == STATE_PROVISIONING) return;

    pollButtons();

    unsigned long now = millis();

    // Frissítési ráta: hangerő képernyőn 100ms, egyébként 500ms normál, 150ms menü
    unsigned long refreshMs = 500;
    if (now < volumeDisplayUntil)                   refreshMs = 100;
    else if (uiState == STATE_MENU)                 refreshMs = 150;
    else if (_playingType.length() && uiState == STATE_NORMAL) refreshMs = 100; // villogáshoz

    // Aktív kommunikáció esetén 100ms frissítés (töltő kör animáció)
    if ((now - _netActivityMs) < 600)               refreshMs = 100;

    if (now - lastUiUpdate >= refreshMs) {
        updateDisplay();
        lastUiUpdate = now;
    }
}

// ── setSnapStatus ─────────────────────────────────────────────────────────────
void UIManager::setSnapStatus(bool online, const String& info) {
    if (_snapOnline != online || _snapInfo != info) {
        _snapOnline = online;
        _snapInfo   = info;
        Serial.printf("[UI] Snap: %s %s\n", online ? "ONLINE" : "OFFLINE", info.c_str());
    }
}

// ── setBackendOnline ──────────────────────────────────────────────────────────
void UIManager::setBackendOnline(bool online) {
    if (_backendOnline != online) {
        _backendOnline = online;
        Serial.printf("[UI] Backend: %s\n", online ? "ONLINE" : "OFFLINE");
    }
}

// ── setPlayingState ───────────────────────────────────────────────────────────
void UIManager::setPlayingState(const String& type) {
    if (type != _playingType) {
        _playingType    = type;
        _playingStartMs = type.length() ? millis() : 0;
        Serial.printf("[UI] Playing: %s\n", type.length() ? type.c_str() : "STOP");
    }
}

// ── setTelemetry ──────────────────────────────────────────────────────────────
void UIManager::setTelemetry(DeviceTelemetry* tel) { _tel = tel; }

// ── setNetActivity ────────────────────────────────────────────────────────────
// Hívd minden kimenő hálózati kérés előtt.
// Az online ikon 500ms-ig teli körként jelenik meg (aktív kommunikáció).
void UIManager::setNetActivity() {
    _netActivityMs = millis();
}

// ═════════════════════════════════════════════════════════════════════════════
// GOMB STATE MACHINE (nem-blokkoló)
// ═════════════════════════════════════════════════════════════════════════════
void UIManager::pollButtons() {
    unsigned long now = millis();

    // ── Bal gomb ──────────────────────────────────────────────────────────
    bool lRaw = (_uiBtnL || digitalRead(TOUCH_L) == HIGH);
    if (_uiBtnL) _uiBtnL = false;

    switch (_btnLState) {
        case BTN_IDLE:
            if (lRaw) { _btnLState = BTN_PRESSED; _btnLPressMs = now; }
            break;
        case BTN_PRESSED:
            if (!digitalRead(TOUCH_L)) {
                unsigned long dur = now - _btnLPressMs;
                if (dur >= LONG_PRESS_MS) {
                    _btnLState = BTN_IDLE;
                    processLongPressL();
                } else {
                    _btnLState    = BTN_WAIT_DOUBLE;
                    _btnLReleaseMs = now;
                }
            } else if (now - _btnLPressMs >= LONG_PRESS_MS) {
                _btnLState = BTN_IDLE;
                processLongPressL();
            }
            break;
        case BTN_WAIT_DOUBLE:
            if (digitalRead(TOUCH_L) == HIGH && now - _btnLReleaseMs < DOUBLE_CLICK_MS) {
                while (digitalRead(TOUCH_L) == HIGH && millis() - now < 300) delay(5);
                _btnLState = BTN_IDLE;
                processClickL(2);
            } else if (now - _btnLReleaseMs >= DOUBLE_CLICK_MS) {
                _btnLState = BTN_IDLE;
                processClickL(1);
            }
            break;
    }

    // ── Jobb gomb ─────────────────────────────────────────────────────────
    bool rRaw = (_uiBtnR || digitalRead(TOUCH_R) == HIGH);
    if (_uiBtnR) _uiBtnR = false;

    switch (_btnRState) {
        case BTN_IDLE:
            if (rRaw) { _btnRState = BTN_PRESSED; _btnRPressMs = now; }
            break;
        case BTN_PRESSED:
            if (!digitalRead(TOUCH_R)) {
                unsigned long dur = now - _btnRPressMs;
                if (dur >= LONG_PRESS_MS) {
                    _btnRState = BTN_IDLE;
                    processLongPressR();
                } else {
                    _btnRState     = BTN_WAIT_DOUBLE;
                    _btnRReleaseMs = now;
                }
            } else if (now - _btnRPressMs >= LONG_PRESS_MS) {
                _btnRState = BTN_IDLE;
                processLongPressR();
            }
            break;
        case BTN_WAIT_DOUBLE:
            if (digitalRead(TOUCH_R) == HIGH && now - _btnRReleaseMs < DOUBLE_CLICK_MS) {
                while (digitalRead(TOUCH_R) == HIGH && millis() - now < 300) delay(5);
                _btnRState = BTN_IDLE;
                processClickR(2);
            } else if (now - _btnRReleaseMs >= DOUBLE_CLICK_MS) {
                _btnRState = BTN_IDLE;
                processClickR(1);
            }
            break;
    }
}

// ── Gomb akciók ───────────────────────────────────────────────────────────────
void UIManager::processClickL(uint8_t clicks) {
    Serial.printf("[UI] BTN_L click=%d state=%d\n", clicks, uiState);
    if (uiState == STATE_NORMAL)     actionVolumeDown();
    else if (uiState == STATE_MENU) {
        if (clicks == 1) navigateMenuNext();
        else             navigateMenuBack();
    }
}

void UIManager::processLongPressL() {
    Serial.printf("[UI] BTN_L long state=%d\n", uiState);
    if (uiState == STATE_NORMAL) {
        uiState = STATE_MENU; menuPage = MENU_MAIN; mainMenuIndex = 0;
    } else {
        uiState = STATE_NORMAL;
    }
    updateDisplay();
}

void UIManager::processClickR(uint8_t clicks) {
    Serial.printf("[UI] BTN_R click=%d state=%d\n", clicks, uiState);
    if (uiState == STATE_NORMAL)    actionVolumeUp();
    else if (uiState == STATE_MENU) executeMenuAction();
}

void UIManager::processLongPressR() {
    Serial.printf("[UI] BTN_R long\n");
}

// ── Menü navigáció ────────────────────────────────────────────────────────────
void UIManager::navigateMenuNext() {
    if (menuPage == MENU_MAIN) {
        if (++mainMenuIndex > 4) mainMenuIndex = 0;
    } else {
        int maxSub = 0;
        if (mainMenuIndex == 0) maxSub = 1;  // SIGNAL: bellMode, sound
        if (mainMenuIndex == 1) maxSub = 2;  // DISPLAY: clock, count, dimm
        if (mainMenuIndex == 2) maxSub = 2;  // STATUS: NET, DEVICE, SNAP
        if (++subMenuIndex > maxSub) subMenuIndex = 0;
    }
    updateDisplay();
}

void UIManager::navigateMenuBack() {
    if (menuPage == MENU_SUB) menuPage = MENU_MAIN;
    else uiState = STATE_NORMAL;
    updateDisplay();
}

void UIManager::executeMenuAction() {
    if (menuPage == MENU_MAIN) {
        menuPage = MENU_SUB; subMenuIndex = 0; _factoryResetConfirmStep = 0;
        updateDisplay(); return;
    }

    if (mainMenuIndex == 0) {  // SIGNAL
        if (subMenuIndex == 0) { if (++settings.bellMode > 2) settings.bellMode = 0; bell.setBellMode(settings.bellMode); }
        if (subMenuIndex == 1) settings.soundEnabled = !settings.soundEnabled;
        Serial.printf("[UI] SIGNAL bellMode=%d sound=%d\n", settings.bellMode, settings.soundEnabled);
    }
    else if (mainMenuIndex == 1) {  // DISPLAY
        if (subMenuIndex == 0) { if (++settings.clockMode > 2) settings.clockMode = 0; }
        if (subMenuIndex == 1) settings.countEnabled = !settings.countEnabled;
        if (subMenuIndex == 2) { if (++settings.dimmLevel > 5) settings.dimmLevel = 0; applyDimming(); }
        Serial.printf("[UI] DISPLAY clk=%d count=%d dimm=%d\n",
            settings.clockMode, settings.countEnabled, settings.dimmLevel);
    }
    else if (mainMenuIndex == 2) {
        // STATUS – read-only, navigateMenuNext görget
    }
    else if (mainMenuIndex == 3) {  // RESET
        Serial.println("[UI] REBOOT kérés");
        display.clearDisplay(); display.setTextSize(1);
        display.setCursor(0,10); display.print("REBOOTING...");
        display.display(); delay(1000); ESP.restart();
    }
    else if (mainMenuIndex == 4) {  // FULL INIT
        unsigned long now = millis();
        if (_factoryResetConfirmStep == 0) {
            _factoryResetConfirmStep = 1; _factoryResetConfirmTime = now;
            display.clearDisplay(); display.setTextSize(1);
            display.setCursor(0,0);  display.print("!! FULL INIT !!");
            display.setCursor(0,12); display.print("Biztos? Nyomd meg");
            display.setCursor(0,22); display.print("meg egyszer! (5s)");
            display.display(); return;
        } else if (_factoryResetConfirmStep == 1 && (now - _factoryResetConfirmTime) < 5000) {
            _factoryResetConfirmStep = 0;
            Serial.println("[UI] FACTORY RESET!");
            display.clearDisplay(); display.setCursor(0,10); display.print("FULL INIT...");
            display.display(); delay(1000); _store.factoryReset(); delay(500); ESP.restart();
        } else {
            _factoryResetConfirmStep = 0;
            display.clearDisplay(); display.setCursor(0,10); display.print("Megszakitva.");
            display.display(); delay(1000);
        }
    }
    updateDisplay();
}

// ── Hangerő (nem hív audio közben, csak setVolume) ────────────────────────────
void UIManager::actionVolumeUp() {
    uint8_t vol = audio.getVolume();
    if (vol < 10) {
        audio.setVolume(vol + 1);
        Serial.printf("[UI] VOL UP → %d\n", vol + 1);
    }
    volumeDisplayUntil = millis() + VOLUME_DISPLAY_TIME;
    updateDisplay();
}

void UIManager::actionVolumeDown() {
    uint8_t vol = audio.getVolume();
    if (vol > 1) {
        audio.setVolume(vol - 1);
        Serial.printf("[UI] VOL DOWN → %d\n", vol - 1);
    }
    volumeDisplayUntil = millis() + VOLUME_DISPLAY_TIME;
    updateDisplay();
}

// ═════════════════════════════════════════════════════════════════════════════
// KIJELZŐ
// ═════════════════════════════════════════════════════════════════════════════
void UIManager::updateDisplay() {
    display.clearDisplay();
    if      (uiState == STATE_NORMAL && millis() < volumeDisplayUntil) drawVolumeScreen();
    else if (uiState == STATE_NORMAL) drawClockScreen();
    else if (uiState == STATE_MENU)   drawMenuScreen();
    display.display();
}

// ── Normál képernyő ───────────────────────────────────────────────────────────
void UIManager::drawClockScreen() {
    if (settings.clockMode == 0) return;

    struct tm t;
    bool timeValid = network.isTimeSynced() && getLocalTime(&t);

    // ── Felső sor: piktogram státuszsor ──────────────────────────────────
    _drawStatusBar();

    // ── Középső terület: óra VAGY villogó RADIO/UZENET ────────────────────
    unsigned long now = millis();
    bool showPlaying  = _playingType.length() > 0 &&
                        (now - _playingStartMs) < PLAYING_FLASH_MS;

    if (showPlaying) {
        // 1Hz villogás: 500ms on, 500ms off
        bool flashOn = ((now / 500) % 2) == 0;
        if (flashOn) {
            display.setTextSize(settings.clockMode == 2 ? 4 : 3);
            display.setCursor(0, settings.clockMode == 2 ? 2 : 9);
            display.print(_playingType);
        }
    } else {
        // Playing timeout → töröljük
        if (_playingType.length() && (now - _playingStartMs) >= PLAYING_FLASH_MS) {
            _playingType = "";
        }

        if (settings.clockMode == 2 && timeValid) {
            // Nagy óra, nincs extra info
            display.setTextSize(4);
            display.setCursor(4, 2);
            if (t.tm_hour < 10) display.print("0");
            display.print(t.tm_hour);
            display.print((now % 1000) < 500 ? ":" : " ");
            if (t.tm_min < 10) display.print("0");
            display.print(t.tm_min);
        } else if (timeValid) {
            // Normál óra + másodperc – y=9-től indul, státuszsor y=0-7 felett van
            display.setTextSize(3);
            display.setCursor(0, 9);
            if (t.tm_hour < 10) display.print("0");
            display.print(t.tm_hour);
            display.print((now % 1000) < 500 ? ":" : " ");
            if (t.tm_min < 10) display.print("0");
            display.print(t.tm_min);
            display.setTextSize(2);
            display.print(":");
            if (t.tm_sec < 10) display.print("0");
            display.print(t.tm_sec);
        } else {
            display.setTextSize(1);
            display.setCursor(0, 16);
            display.print("Syncing time...");
        }
    }

    // ── Visszaszámláló csengetésig ────────────────────────────────────────
    if (settings.countEnabled && !showPlaying) {
        int secToBell = bell.getSecondsToNextEvent();
        if (secToBell > 0 && secToBell <= 60) {
            display.setTextSize(1);
            display.setCursor(100, 24);
            display.print(secToBell);
            display.print("s");
        }
    }
}

// ── Menü képernyő ─────────────────────────────────────────────────────────────
void UIManager::drawMenuScreen() {
    display.setTextSize(1);
    display.setCursor(0, 0);

    if (menuPage == MENU_MAIN) {
        display.print("MAIN MENU");
    } else {
        const char* titles[] = { "SIGNAL SET", "DISPLAY SET", "STATUS INFO", "SYSTEM", "FULL INIT" };
        if (mainMenuIndex >= 0 && mainMenuIndex <= 4) display.print(titles[mainMenuIndex]);
    }

    display.setTextSize(2);
    display.setCursor(0, 16);

    if (menuPage == MENU_MAIN) {
        const char* items[] = { "SIGNAL >", "DISPLAY >", "STATUS >", "RESET >", "FULL INIT>" };
        if (mainMenuIndex >= 0 && mainMenuIndex <= 4) display.print(items[mainMenuIndex]);
        return;
    }

    if (mainMenuIndex == 0) {  // SIGNAL
        if (subMenuIndex == 0) {
            display.print("BELL:");
            if      (settings.bellMode == 0) display.print("OFF");
            else if (settings.bellMode == 1) display.print("ON");
            else                             display.print("TODAY");
        }
        if (subMenuIndex == 1) { display.print("SND:"); display.print(settings.soundEnabled ? "ON" : "OFF"); }
    }
    else if (mainMenuIndex == 1) {  // DISPLAY
        if (subMenuIndex == 0) {
            display.print("CLK:");
            if      (settings.clockMode == 0) display.print("OFF");
            else if (settings.clockMode == 1) display.print("ON");
            else                              display.print("FULL");
        }
        if (subMenuIndex == 1) { display.print("CNT:"); display.print(settings.countEnabled ? "ON" : "OFF"); }
        if (subMenuIndex == 2) { display.print("DIM:"); display.print(settings.dimmLevel); }
    }
    else if (mainMenuIndex == 2) {  // STATUS
        drawStatusScreen();
        return;
    }
    else if (mainMenuIndex == 3) {
        display.print("REBOOT ?");
    }
    else if (mainMenuIndex == 4) {
        if (_factoryResetConfirmStep == 0) display.print("FULL INIT?");
        else { display.setTextSize(1); display.print("Meg egyszer!"); }
    }
}

// ── Status képernyő ───────────────────────────────────────────────────────────
void UIManager::drawStatusScreen() {
    display.setTextSize(1);

    if (subMenuIndex == 0) {
        // ── NET ───────────────────────────────────────────────────────────
        display.setCursor(0, 0); display.println("        - NET");
        String ip = network.getIP();
        display.setCursor(0, 12); display.print("IP: ");
        display.println(ip.length() ? ip : "-");
        display.setCursor(0, 22); display.print("RSSI: ");
        display.print(network.getRSSI()); display.print("dBm");
        display.setCursor(0, 32); display.print("SRV: ");
        if (_tel) {
            bool ok = _tel->serverReachable && (millis() - _tel->lastServerOkMs) < 60000;
            display.print(ok ? "ONLINE" : "OFFLINE");
        } else { display.print("N/A"); }
    }
    else if (subMenuIndex == 1) {
        // ── DEVICE ────────────────────────────────────────────────────────
        display.setCursor(0, 0); display.println("      - DEVICE");
        display.setCursor(0, 12); display.print("FW: ");
        display.println(_tel ? _tel->firmwareVersion : String(FW_VERSION));
        display.setCursor(0, 22); display.print("ID: ");
        if (_tel && _tel->deviceId.length()) display.println(_tel->deviceId.substring(0, 12));
        else display.println("N/A");
        display.setCursor(0, 32);
        if (_tel) {
            display.print("ERR P:"); display.print(_tel->pollErr);
            display.print(" B:");    display.print(_tel->beaconErr);
            display.print(" A:");    display.print(_tel->ackErr);
        }
        display.setCursor(0, 42); display.print("LAST: ");
        if (_tel && _tel->lastError.length()) {
            String e = _tel->lastError;
            if (e.length() > 14) e = e.substring(0, 14);
            display.print(e);
        } else { display.print("-"); }
    }
    else if (subMenuIndex == 2) {
        // ── SNAP ──────────────────────────────────────────────────────────
        display.setCursor(0, 0); display.println("        - SNAP");
        display.setCursor(0, 12); display.print("SNAP: ");
        display.println(_snapOnline ? "ONLINE" : "OFFLINE");
        display.setCursor(0, 22); display.print("SRV:  ");
        display.println(_backendOnline ? "ONLINE" : "OFFLINE");
        display.setCursor(0, 32); display.print("SYS:  ");
        display.println((_snapOnline && _backendOnline) ? "OK" : "FAIL");
        if (_snapInfo.length()) {
            display.setCursor(0, 42); display.print(_snapInfo.substring(0, 21));
        }
    }
}

// ── Hangerő képernyő ──────────────────────────────────────────────────────────
void UIManager::drawVolumeScreen() {
    display.setTextSize(1);
    display.setCursor(0, 0); display.print("VOLUME");
    display.setTextSize(4);
    uint8_t vol = audio.getVolume();
    display.setCursor(vol == 10 ? 40 : 52, 4);
    display.print(vol);
}

// ── Splash ────────────────────────────────────────────────────────────────────
void UIManager::drawSplashScreen() {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);   display.println("SchoolLive!");
    display.setTextSize(1);
    display.setCursor(0, 20);  display.print("SmartSpeaker  ");
    display.println(FW_VERSION);
    display.display();
    Serial.printf("[UI] Splash: SchoolLive %s\n", FW_VERSION);
}

// ── Boot státusz ──────────────────────────────────────────────────────────────
void UIManager::drawBootStatus(String status, String details) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);  display.print("BOOTING...");
    display.setCursor(0, 12); display.print(status);
    display.setCursor(0, 24); display.print(details);
    display.display();
    Serial.printf("[UI] Boot: %s | %s\n", status.c_str(), details.c_str());
}

// ── Provisioning ──────────────────────────────────────────────────────────────
void UIManager::enterProvisioningMode() {
    uiState = STATE_PROVISIONING;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0); display.print("Var aktivalasra...");
    display.display();
    Serial.println("[UI] Provisioning mód");
}

void UIManager::updateProvisioningDisplay(const String& mac, const String& ip,
                                           const String& status) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);  display.print("MAC:"); display.print(mac);
    display.setCursor(0, 11); display.print("IP: ");  display.print(ip.length() ? ip : "...");
    display.setCursor(0, 22); display.print(status);
    display.display();
}

// ── Dimming ───────────────────────────────────────────────────────────────────
// 5 fokozat: kontrast = 255, 205, 155, 105, 55, 5
// Az 5. fokozat = 5/255 ≈ 2% → ez az SSD1306 gyakorlati minimuma,
// tovább nem csökkenthető értelmesen. Marad 5 fokozat.
void UIManager::applyDimming() {
    uint8_t contrast = 255;
    if (settings.dimmLevel > 0) {
        contrast = 255 - (settings.dimmLevel * 50);
        if (contrast < 5) contrast = 5;
    }
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(contrast);
}

// ═════════════════════════════════════════════════════════════════════════════
// STÁTUSZSOR PIKTOGRAMOK (felső 8px sor)
// ═════════════════════════════════════════════════════════════════════════════

// ── Teljes státuszsor rajzolása ───────────────────────────────────────────────
// Layout (128x8px):
//   x=0  .. 11 : WiFi jelerősség (4 bár)
//   x=14 .. 21 : Online ikon (kör+pipa / teli kör / kör+X)
//   x=25 .. 33 : Snap ikon (hangszóró+hullám / hangszóró+slash)
//   x=92 .. 99 : MUTE felirat (ha bellMode == 0)
void UIManager::_drawStatusBar() {
    // ── WiFi barok ────────────────────────────────────────────────────────
    // Érvényes RSSI értékek negatívak; 0 / pozitív = nincs WiFi
    int32_t rssi = network.getRSSI();
    int8_t bars = 0;
    if (rssi < 0) {
        if      (rssi >= -60) bars = 4;
        else if (rssi >= -70) bars = 3;
        else if (rssi >= -80) bars = 2;
        else                  bars = 1;
    }
    _drawWifiIcon(0, 0, bars);

    // ── Online ikon ───────────────────────────────────────────────────────
    bool sysOnline = _backendOnline && _snapOnline;
    bool netActive = (millis() - _netActivityMs) < 500;
    _drawOnlineIcon(14, 0, sysOnline, netActive);

    // ── Snap ikon ─────────────────────────────────────────────────────────
    _drawSnapIcon(25, 0, _snapOnline);

    // ── MUTE jelzés ───────────────────────────────────────────────────────
    if (settings.bellMode == 0) {
        display.setTextSize(1);
        display.setCursor(92, 0);
        display.print("MUTE");
    }
}

// ── WiFi jelerősség ikon ──────────────────────────────────────────────────────
// 4 függőleges bár, alulra igazítva, összesen ~11x8 px
// bars=0: mind üres körvonal (nincs WiFi)
// bars=1-4: az alsó N bár teli, a többi körvonal
void UIManager::_drawWifiIcon(int16_t x, int16_t y, int8_t bars) {
    for (int i = 0; i < 4; i++) {
        int16_t bh = 2 + i * 2;       // magasságok: 2, 4, 6, 8 px
        int16_t bx = x + i * 3;       // x pozíciók: 0, 3, 6, 9
        int16_t by = y + 8 - bh;      // alulra igazítva
        if (i < bars) {
            display.fillRect(bx, by, 2, bh, SSD1306_WHITE);   // aktív bár: teli
        } else {
            display.drawRect(bx, by, 2, bh, SSD1306_WHITE);   // inaktív bár: körvonal
        }
    }
}

// ── Online állapot ikon ───────────────────────────────────────────────────────
// Elhelyezés: 8x8 px, kör középpontja (x+3, y+3), r=3
//
// Állapotok:
//   online=false            → üres kör + X  (offline)
//   online=true, active=false → üres kör + pipa  (csatlakozva, nem kommunikál)
//   online=true, active=true  → teli kör  (aktív kommunikáció)
void UIManager::_drawOnlineIcon(int16_t x, int16_t y, bool online, bool active) {
    if (!online) {
        // Offline: üres kör + X
        display.drawCircle(x + 3, y + 3, 3, SSD1306_WHITE);
        display.drawLine(x + 1, y + 1, x + 5, y + 5, SSD1306_WHITE);
        display.drawLine(x + 5, y + 1, x + 1, y + 5, SSD1306_WHITE);
    } else if (active) {
        // Aktív kommunikáció: teli kör (pulzál a 100ms frissítéssel)
        display.fillCircle(x + 3, y + 3, 3, SSD1306_WHITE);
    } else {
        // Online, idle: üres kör + pipa
        display.drawCircle(x + 3, y + 3, 3, SSD1306_WHITE);
        // Pipa: (x+1,y+3) → (x+2,y+5) → (x+5,y+1)
        display.drawLine(x + 1, y + 3, x + 2, y + 5, SSD1306_WHITE);
        display.drawLine(x + 2, y + 5, x + 5, y + 1, SSD1306_WHITE);
    }
}

// ── Snap (hangszóró) ikon ─────────────────────────────────────────────────────
// Elhelyezés: 9x8 px
//
// connected=true  → hangszóró + kis hullám (Snapcast csatlakozva)
// connected=false → hangszóró + slash (nem csatlakozva)
void UIManager::_drawSnapIcon(int16_t x, int16_t y, bool connected) {
    // Hangszóró test (teli téglalap)
    display.fillRect(x, y + 2, 3, 4, SSD1306_WHITE);

    // Kúp (trapézszerű, jobbra kiszélesedő)
    display.drawLine(x + 2, y + 1, x + 5, y + 0, SSD1306_WHITE);
    display.drawLine(x + 5, y + 0, x + 5, y + 7, SSD1306_WHITE);
    display.drawLine(x + 5, y + 7, x + 2, y + 6, SSD1306_WHITE);
    display.drawLine(x + 2, y + 6, x + 2, y + 1, SSD1306_WHITE);

    if (connected) {
        // Hangullám: kis C-ív a hangszóró jobb oldalán
        display.drawLine(x + 6, y + 2, x + 7, y + 1, SSD1306_WHITE);
        display.drawLine(x + 7, y + 1, x + 8, y + 2, SSD1306_WHITE);
        display.drawLine(x + 8, y + 2, x + 8, y + 5, SSD1306_WHITE);
        display.drawLine(x + 8, y + 5, x + 7, y + 6, SSD1306_WHITE);
        display.drawLine(x + 7, y + 6, x + 6, y + 5, SSD1306_WHITE);
    } else {
        // Mute slash: átlós vonal a kúpon és jobbra
        display.drawLine(x + 3, y + 6, x + 7, y + 1, SSD1306_WHITE);
    }
}