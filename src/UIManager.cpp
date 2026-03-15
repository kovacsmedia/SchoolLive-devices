#include "UIManager.h"

// --- INTERRUPT ---
volatile bool flagL = false;
volatile bool flagR = false;
volatile unsigned long lastIntTime = 0;

void IRAM_ATTR isrL() {
    if (millis() - lastIntTime > 50) { flagL = true; lastIntTime = millis(); }
}
void IRAM_ATTR isrR() {
    if (millis() - lastIntTime > 50) { flagR = true; lastIntTime = millis(); }
}

// --- KONSTRUKTOR ---
UIManager::UIManager(AudioManager &audioMgr, NetworkManager &netMgr, BellManager &bellMgr, PersistStore &storeRef)
    : audio(audioMgr), network(netMgr), bell(bellMgr), _store(storeRef),
      display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1) {}

// --- BEGIN ---
void UIManager::begin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
    delay(100);

    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    display.setRotation(2);
    display.setTextColor(SSD1306_WHITE);
    display.setTextWrap(false);

    applyDimming();
    drawSplashScreen();
    delay(2000);

    pinMode(TOUCH_L, INPUT);
    pinMode(TOUCH_R, INPUT);

    attachInterrupt(digitalPinToInterrupt(TOUCH_L), isrL, RISING);
    attachInterrupt(digitalPinToInterrupt(TOUCH_R), isrR, RISING);

    bell.setBellMode(settings.bellMode);
}

// --- LOOP ---
void UIManager::loop() {
    if (uiState == STATE_PROVISIONING) return;

    if (flagL) { flagL = false; handleButtonL(); }
    if (flagR) { flagR = false; handleButtonR(); }

    unsigned long now = millis();

    if (!flagL && !flagR) {
        if (uiState == STATE_NORMAL) {
            unsigned long refresh = (now < volumeDisplayUntil) ? 100 : 500;
            if (now - lastUiUpdate > refresh) {
                updateDisplay();
                lastUiUpdate = now;
            }
        } else {
            if (now - lastUiUpdate > 100) {
                updateDisplay();
                lastUiUpdate = now;
            }
        }
    }
}

// --- GOMB LOGIKA ---
void UIManager::handleButtonL() {
    unsigned long pressStart = millis();
    while (digitalRead(TOUCH_L) == HIGH) {
        if (millis() - pressStart > LONG_PRESS_MS) { processLongPressL(); return; }
        delay(10);
    }
    unsigned long releaseTime = millis();
    bool doubleClick = false;
    while (millis() - releaseTime < 250) {
        if (digitalRead(TOUCH_L) == HIGH) {
            doubleClick = true; while(digitalRead(TOUCH_L) == HIGH); break;
        }
    }
    processClickL(doubleClick ? 2 : 1);
}

void UIManager::handleButtonR() {
    unsigned long pressStart = millis();
    while (digitalRead(TOUCH_R) == HIGH) {
        if (millis() - pressStart > LONG_PRESS_MS) { processLongPressR(); return; }
        delay(10);
    }
    unsigned long releaseTime = millis();
    bool doubleClick = false;
    while (millis() - releaseTime < 250) {
        if (digitalRead(TOUCH_R) == HIGH) {
            doubleClick = true; while(digitalRead(TOUCH_R) == HIGH); break;
        }
    }
    processClickR(doubleClick ? 2 : 1);
}

// --- ESEMÉNYEK ---
void UIManager::processClickL(uint8_t clicks) {
    if (uiState == STATE_NORMAL) actionVolumeDown(false);
    else if (uiState == STATE_MENU) {
        if (clicks == 1) navigateMenuNext();
        else if (clicks == 2) navigateMenuBack();
    }
}

void UIManager::processLongPressL() {
    if (uiState == STATE_NORMAL) {
        uiState = STATE_MENU;
        menuPage = MENU_MAIN;
        mainMenuIndex = 0;
        updateDisplay();
    } else {
        uiState = STATE_NORMAL;
        updateDisplay();
    }
}

void UIManager::processClickR(uint8_t clicks) {
    if (uiState == STATE_NORMAL) actionVolumeUp(false);
    else if (uiState == STATE_MENU) {
        if (clicks == 1) executeMenuAction();
    }
}

void UIManager::processLongPressR() {}

// --- MENÜ ---
void UIManager::navigateMenuNext() {
    if (menuPage == MENU_MAIN) {
        mainMenuIndex++;
        if (mainMenuIndex > 4) mainMenuIndex = 0;
    } else {
        subMenuIndex++;
        int maxSub = 0;
        if (mainMenuIndex == 0) maxSub = 1;
        if (mainMenuIndex == 1) maxSub = 2;
        if (mainMenuIndex == 2) maxSub = 1;
        if (mainMenuIndex == 3) maxSub = 0;
        if (mainMenuIndex == 4) maxSub = 0;
        if (subMenuIndex > maxSub) subMenuIndex = 0;
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
        menuPage = MENU_SUB;
        subMenuIndex = 0;
        _factoryResetConfirmStep = 0;
        updateDisplay();
        return;
    }

    if (mainMenuIndex == 0) { // SIGNAL
        if (subMenuIndex == 0) {
            settings.bellMode++;
            if (settings.bellMode > 2) settings.bellMode = 0;
            bell.setBellMode(settings.bellMode);
        } else if (subMenuIndex == 1) {
            settings.soundEnabled = !settings.soundEnabled;
        }
    }
    else if (mainMenuIndex == 1) { // DISPLAY
        if (subMenuIndex == 0) {
            settings.clockMode++;
            if (settings.clockMode > 2) settings.clockMode = 0;
        } else if (subMenuIndex == 1) {
            settings.countEnabled = !settings.countEnabled;
        } else if (subMenuIndex == 2) {
            settings.dimmLevel++;
            if (settings.dimmLevel > 5) settings.dimmLevel = 0;
            applyDimming();
        }
    }
    else if (mainMenuIndex == 3) { // RESET
        if (subMenuIndex == 0) {
            display.clearDisplay();
            display.setCursor(0, 10);
            display.print("REBOOTING...");
            display.display();
            delay(1000);
            ESP.restart();
        }
    }
    else if (mainMenuIndex == 4) { // FULL INIT
        if (subMenuIndex == 0) {
            unsigned long now = millis();

            if (_factoryResetConfirmStep == 0) {
                _factoryResetConfirmStep = 1;
                _factoryResetConfirmTime = now;
                display.clearDisplay();
                display.setTextSize(1);
                display.setCursor(0, 0);
                display.print("!! FULL INIT !!");
                display.setCursor(0, 12);
                display.print("Biztos? Nyomd meg");
                display.setCursor(0, 22);
                display.print("meg egyszer! (5s)");
                display.display();
                return;
            } else if (_factoryResetConfirmStep == 1 &&
                       (now - _factoryResetConfirmTime) < 5000) {
                _factoryResetConfirmStep = 0;
                display.clearDisplay();
                display.setTextSize(1);
                display.setCursor(0, 10);
                display.print("FULL INIT...");
                display.display();
                delay(1000);
                _store.factoryReset();
                delay(500);
                ESP.restart();
            } else {
                _factoryResetConfirmStep = 0;
                display.clearDisplay();
                display.setTextSize(1);
                display.setCursor(0, 10);
                display.print("Megszakitva.");
                display.display();
                delay(1000);
                updateDisplay();
            }
        }
    }

    updateDisplay();
}

// --- HANGERŐ ---
void UIManager::actionVolumeUp(bool beep) {
    uint8_t vol = audio.getVolume();
    if (vol < 10) { audio.setVolume(vol + 1); if (beep) playFeedback(); }
    volumeDisplayUntil = millis() + VOLUME_DISPLAY_TIME;
    updateDisplay();
}

void UIManager::actionVolumeDown(bool beep) {
    uint8_t vol = audio.getVolume();
    if (vol > 1) { audio.setVolume(vol - 1); if (beep) playFeedback(); }
    volumeDisplayUntil = millis() + VOLUME_DISPLAY_TIME;
    updateDisplay();
}

void UIManager::playFeedback() {}

// --- RAJZOLÁS ---
void UIManager::updateDisplay() {
    display.clearDisplay();

    if (uiState == STATE_NORMAL) {
        if (millis() < volumeDisplayUntil) drawVolumeScreen();
        else if (settings.clockMode > 0) drawClockScreen();
        else display.display();
    } else if (uiState == STATE_MENU) {
        drawMenuScreen();
    }

    display.display();
}

void UIManager::drawStatusScreen() {
    display.setTextSize(1);
    display.setCursor(0, 0);

    if (subMenuIndex == 0) {
        display.println("            - NET");
        String ip = network.getIP();
        display.setCursor(0, 12);
        display.print("IP: ");
        display.println(ip.length() ? ip : "-");
        display.setCursor(0, 22);
        display.print("RSSI: ");
        display.print(network.getRSSI());
        display.print(" dBm");
        display.setCursor(0, 32);
        display.print("SRV: ");
        if (_tel) {
            bool ok = _tel->serverReachable && (millis() - _tel->lastServerOkMs) < 60000;
            display.print(ok ? "ONLINE" : "OFFLINE");
        } else {
            display.print("N/A");
        }
    } else {
        display.println("          - DEVICE");
        display.setCursor(0, 12);
        display.print("FW: ");
        display.println(_tel ? _tel->firmwareVersion : String(FW_VERSION));
        display.setCursor(0, 22);
        display.print("ID: ");
        if (_tel && _tel->deviceId.length()) {
            display.println(_tel->deviceId);
        } else {
            display.println("N/A");
        }
        display.setCursor(0, 32);
        if (_tel) {
            display.print("ERR P:");
            display.print(_tel->pollErr);
            display.print(" B:");
            display.print(_tel->beaconErr);
            display.print(" A:");
            display.print(_tel->ackErr);
        } else {
            display.print("ERR: N/A");
        }
        display.setCursor(0, 42);
        display.print("LAST: ");
        if (_tel && _tel->lastError.length()) {
            String e = _tel->lastError;
            if (e.length() > 14) e = e.substring(0, 14);
            display.print(e);
        } else {
            display.print("-");
        }
    }
}

// --- CLOCK SCREEN ---
void UIManager::drawClockScreen() {
    struct tm t;
    bool timeValid = network.isTimeSynced() && getLocalTime(&t);
    int secToBell = bell.getSecondsToNextEvent();
    bool showCount = settings.countEnabled && secToBell > 0 && secToBell <= 60;

    if (showCount) {
        display.setTextSize(4);
        display.setCursor(30, 2);
        display.print(secToBell);
    }
    else if (settings.clockMode == 2 && timeValid) {
        display.setTextSize(4);
        display.setCursor(4, 2);
        if (t.tm_hour < 10) display.print("0");
        display.print(t.tm_hour);
        display.print((millis() % 1000) < 500 ? ":" : " ");
        if (t.tm_min < 10) display.print("0");
        display.print(t.tm_min);
    }
    else if (timeValid) {
        display.setTextSize(1);
        display.setCursor(0, 0);
        String ssid = network.getCurrentSSID();
        display.print(ssid);
        int32_t rssi = network.getRSSI();
        int quality = map(rssi, -100, -50, 0, 100);
        if (quality < 0) quality = 0;
        if (quality > 100) quality = 100;
        display.print(" ");
        display.print(quality);
        display.print("%");

        display.setTextSize(3);
        display.setCursor(0, 9);
        if (t.tm_hour < 10) display.print("0");
        display.print(t.tm_hour);
        display.print((millis() % 1000) < 500 ? ":" : " ");
        if (t.tm_min < 10) display.print("0");
        display.print(t.tm_min);
        display.setTextSize(2);
        display.print(":");
        if (t.tm_sec < 10) display.print("0");
        display.print(t.tm_sec);

        display.setTextSize(1);
        display.setCursor(95, 0);
        if (settings.bellMode == 0) display.print("MUTE");
        else display.print(bell.getNextEventTimeStr());
    }
    else {
        display.setCursor(0, 16);
        display.print("Syncing Time...");
    }
}

void UIManager::drawMenuScreen() {
    display.setTextSize(1);
    display.setCursor(0, 0);

    if (menuPage == MENU_MAIN) {
        display.print("MAIN MENU");
    } else {
        switch(mainMenuIndex) {
            case 0: display.print("SIGNAL SET"); break;
            case 1: display.print("DISPLAY SET"); break;
            case 2: display.print("STATUS INFO"); break;
            case 3: display.print("SYSTEM"); break;
            case 4: display.print("FULL INIT"); break;
        }
    }

    display.setTextSize(2);
    display.setCursor(0, 16);

    if (menuPage == MENU_MAIN) {
        switch(mainMenuIndex) {
            case 0: display.print("SIGNAL >"); break;
            case 1: display.print("DISPLAY >"); break;
            case 2: display.print("STATUS >"); break;
            case 3: display.print("RESET >"); break;
            case 4: display.print("FULL INIT>"); break;
        }
    } else {
        if (mainMenuIndex == 0) {
            if (subMenuIndex == 0) {
                display.print("BELL: ");
                if (settings.bellMode == 0) display.print("OFF");
                else if (settings.bellMode == 1) display.print("ON");
                else display.print("TODAY");
            }
            if (subMenuIndex == 1) display.printf("SOUND:%s", settings.soundEnabled ? "ON" : "OFF");
        }
        else if (mainMenuIndex == 1) {
            if (subMenuIndex == 0) {
                display.print("CLK:");
                if (settings.clockMode == 0) display.print("OFF");
                else if (settings.clockMode == 1) display.print("ON");
                else display.print("FULL");
            }
            if (subMenuIndex == 1) display.printf("COUNT:%s", settings.countEnabled ? "ON" : "OFF");
            if (subMenuIndex == 2) display.printf("DIMM: %d", settings.dimmLevel);
        }
        else if (mainMenuIndex == 2) {
            drawStatusScreen();
        }
        else if (mainMenuIndex == 3) {
            if (subMenuIndex == 0) display.print("REBOOT ?");
        }
        else if (mainMenuIndex == 4) {
            if (subMenuIndex == 0) {
                if (_factoryResetConfirmStep == 0) {
                    display.print("FULL INIT?");
                } else {
                    display.setTextSize(1);
                    display.print("Meg egyszer!");
                }
            }
        }
    }
}

void UIManager::drawVolumeScreen() {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("VOLUME");
    display.setTextSize(4);
    uint8_t vol = audio.getVolume();
    int x = (vol == 10) ? 40 : 52;
    display.setCursor(x, 4);
    display.print(vol);
}

void UIManager::drawSplashScreen() {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("SchoolLive!");
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.print("SmartSpeaker  ");
    display.println(FW_VERSION);   // ← Config.h-ból, nem hardkódolt
    display.display();
}

void UIManager::applyDimming() {
    uint8_t contrast = 255;
    if (settings.dimmLevel > 0) {
        contrast = 255 - (settings.dimmLevel * 50);
        if (contrast < 5) contrast = 5;
    }
    display.ssd1306_command(SSD1306_SETCONTRAST);
    display.ssd1306_command(contrast);
}

void UIManager::drawBootStatus(String status, String details) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("BOOTING...");
    display.setCursor(0, 12);
    display.print(status);
    display.setCursor(0, 24);
    display.print(details);
    display.display();
}

void UIManager::enterProvisioningMode() {
    uiState = STATE_PROVISIONING;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Var aktivalasra...");
    display.display();
}

void UIManager::updateProvisioningDisplay(const String& mac, const String& ip, const String& status) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("MAC:");
    display.print(mac);
    display.setCursor(0, 11);
    display.print("IP: ");
    display.print(ip.length() > 0 ? ip : "...");
    display.setCursor(0, 22);
    display.print(status);
    display.display();
}

void UIManager::setTelemetry(DeviceTelemetry* tel) {
    _tel = tel;
}