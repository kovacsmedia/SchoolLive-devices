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
UIManager::UIManager(AudioManager &audioMgr, NetworkManager &netMgr, BellManager &bellMgr) 
    : audio(audioMgr), network(netMgr), bell(bellMgr),
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
    if (flagL) { flagL = false; handleButtonL(); }
    if (flagR) { flagR = false; handleButtonR(); }

    unsigned long now = millis();
    
    if (uiState == STATE_NETRADIO) checkStreamHealth();

    if (!flagL && !flagR) {
        if (uiState == STATE_NORMAL) {
            // Frissítés logika: Ha hangerőt állítunk, akkor gyorsan, amúgy mp-enként (óra miatt)
            // Vagy ha az animációt akarjuk (kettőspont), akkor gyakrabban (500ms)
            unsigned long refresh = (now < volumeDisplayUntil) ? 250 : 500;
            
            if (now - lastUiUpdate > refresh) {
                updateDisplay(); lastUiUpdate = now;
            }
        } else {
            // Menü és Rádió gyorsabb
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
        if (clicks == 1) navigateMenuNext(); else if (clicks == 2) navigateMenuBack();
    }
    else if (uiState == STATE_NETRADIO) {
        if (clicks == 1) actionVolumeDown(false); else if (clicks == 2) exitNetRadio();
    }
}

void UIManager::processLongPressL() {
    if (uiState == STATE_NORMAL) {
        uiState = STATE_MENU; menuPage = MENU_MAIN; mainMenuIndex = 0; updateDisplay();
    } else if (uiState == STATE_NETRADIO) {
        exitNetRadio();
    }
}

void UIManager::processClickR(uint8_t clicks) {
    if (uiState == STATE_NORMAL) actionVolumeUp(false);
    else if (uiState == STATE_MENU) {
        if (clicks == 1) executeMenuAction();
    }
    else if (uiState == STATE_NETRADIO) {
        if (clicks == 1) actionVolumeUp(false); 
        else if (clicks == 2) playNextStation(); 
    }
}

void UIManager::processLongPressR() {}

// --- MENÜ ---
void UIManager::navigateMenuNext() {
    if (menuPage == MENU_MAIN) { 
        mainMenuIndex++; 
        if (mainMenuIndex > 4) mainMenuIndex = 0; 
    }
    else { subMenuIndex++; int maxSub=0; if(mainMenuIndex==0) maxSub=1; if(mainMenuIndex==1) maxSub=2; if(mainMenuIndex==2) maxSub=1; if(mainMenuIndex==3) maxSub=0; if(subMenuIndex>maxSub) subMenuIndex=0; }
    updateDisplay();
}
void UIManager::navigateMenuBack() { if (menuPage == MENU_SUB) menuPage = MENU_MAIN; else uiState = STATE_NORMAL; updateDisplay(); }
void UIManager::executeMenuAction() {
    if (menuPage == MENU_MAIN) {
        if (mainMenuIndex == 4) enterNetRadio(); 
        else { menuPage = MENU_SUB; subMenuIndex = 0; }
    } else {
        if (mainMenuIndex == 0) { // SIGNAL
            if (subMenuIndex == 0) { 
                settings.bellMode++;
                if (settings.bellMode > 2) settings.bellMode = 0;
                bell.setBellMode(settings.bellMode);
            }
            else if (subMenuIndex == 1) settings.soundEnabled = !settings.soundEnabled;
        }
        else if (mainMenuIndex == 1) { // DISPLAY
            if (subMenuIndex == 0) { settings.clockMode++; if (settings.clockMode > 2) settings.clockMode = 0; }
            else if (subMenuIndex == 1) settings.countEnabled = !settings.countEnabled;
            else if (subMenuIndex == 2) { settings.dimmLevel++; if (settings.dimmLevel > 5) settings.dimmLevel = 0; applyDimming(); }
        }
        else if (mainMenuIndex == 3) { // RESET
            if (subMenuIndex == 0) { display.clearDisplay(); display.setCursor(0,10); display.print("REBOOTING..."); display.display(); delay(1000); ESP.restart(); }
        }
    } updateDisplay();
}

// --- NETRADIO ---
void UIManager::enterNetRadio() {
    uiState = STATE_NETRADIO; display.clearDisplay(); display.setCursor(0,10); display.print("Loading..."); display.display();
    String listData = network.fetchFile(RADIOLIST_URL); parseRadioList(listData);
    if (radioList.size() > 0) {
        currentStationIndex = 0; audio.playUrl(radioList[0].url.c_str()); streamStalledTime = 0; isRadioPlaying = true;
    } else {
        display.setCursor(0,20); display.print("Error!"); display.display(); delay(2000); uiState = STATE_MENU;
    }
    updateDisplay();
}
void UIManager::exitNetRadio() { audio.stop(); isRadioPlaying = false; uiState = STATE_NORMAL; updateDisplay(); }

void UIManager::playNextStation() {
    if (radioList.size() == 0) return;
    currentStationIndex++; if (currentStationIndex >= radioList.size()) currentStationIndex = 0;
    audio.stop(); isRadioPlaying = false; audio.playUrl(radioList[currentStationIndex].url.c_str()); streamStalledTime = 0; updateDisplay();
}

void UIManager::playCurrentStation() {
    if (radioList.size() == 0) return;
    audio.stop(); isRadioPlaying = false; audio.playUrl(radioList[currentStationIndex].url.c_str()); streamStalledTime = 0; updateDisplay();
}

void UIManager::checkStreamHealth() {
    if (!audio.isStreamMode()) {
        if (!audio.isPlaying()) playCurrentStation(); 
        return; 
    }
    if (!audio.isPlaying()) {
        isRadioPlaying = false;
        if (streamStalledTime == 0) streamStalledTime = millis();
        if (millis() - streamStalledTime > STREAM_TIMEOUT_MS) { playNextStation(); streamStalledTime = 0; }
    } else { isRadioPlaying = true; streamStalledTime = 0; }
}

void UIManager::parseRadioList(String data) {
    radioList.clear(); int start = 0; int end = data.indexOf('\n');
    while (end != -1 || start < data.length()) {
        String line; if (end == -1) line = data.substring(start); else line = data.substring(start, end); line.trim();
        if (line.length() > 0) {
            int separator = line.indexOf('|');
            if (separator != -1) { RadioStation s; s.name = line.substring(0, separator); s.url = line.substring(separator + 1); radioList.push_back(s); }
        }
        if (end == -1) break; start = end + 1; end = data.indexOf('\n', start);
    }
}

// --- HANGERŐ ---
void UIManager::actionVolumeUp(bool beep) {
    uint8_t vol = audio.getVolume();
    if (vol < 10) { audio.setVolume(vol + 1); if (beep) playFeedback(); }
    if (uiState != STATE_NETRADIO) volumeDisplayUntil = millis() + VOLUME_DISPLAY_TIME;
    updateDisplay();
}
void UIManager::actionVolumeDown(bool beep) {
    uint8_t vol = audio.getVolume();
    if (vol > 1) { audio.setVolume(vol - 1); if (beep) playFeedback(); }
    if (uiState != STATE_NETRADIO) volumeDisplayUntil = millis() + VOLUME_DISPLAY_TIME;
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
    } else if (uiState == STATE_NETRADIO) {
        drawNetRadioScreen();
    }
    display.display();
}

void UIManager::drawStatusScreen() {
    display.setTextSize(1); display.setCursor(0,0);
    if (subMenuIndex == 0) { 
        display.println("            - NET");
        display.setCursor(0,12); display.print("IP: "); display.println(network.getIP());
        display.setCursor(0,22); display.print("SIG: "); display.print(network.getRSSI()); display.print(" dBm");
    } else { 
        display.println("            - DEVICE");
        display.setCursor(0,12); display.print("ID: "); display.println(DEVICE_ID);
        display.setCursor(0,22); display.print("RAM: "); 
        float freeRam = ESP.getFreeHeap(); int percent = (freeRam / 320000.0) * 100;
        display.print(percent); display.print("% Free");
    }
}

// --- ÚJ CLOCK SCREEN ---
void UIManager::drawClockScreen() {
    struct tm t; bool timeValid = network.isTimeSynced() && getLocalTime(&t);
    int secToBell = bell.getSecondsToNextEvent();
    bool showCount = settings.countEnabled && secToBell > 0 && secToBell <= 60;

    if (showCount) {
        // Visszaszámlálás
        display.setTextSize(4); display.setCursor(30, 2); display.print(secToBell);
    } 
    else if (settings.clockMode == 2 && timeValid) {
        // FULL CLOCK
        display.setTextSize(4); display.setCursor(4, 2); 
        if(t.tm_hour < 10) display.print("0"); display.print(t.tm_hour);
        display.print((millis() % 1000) < 500 ? ":" : " ");
        if(t.tm_min < 10) display.print("0"); display.print(t.tm_min);
    }
    else if (timeValid) {
        // --- 1. SOR: SSID + SIGNAL % ---
        display.setTextSize(1); display.setCursor(0,0);
        
        // SSID kiírása (ha túl hosszú, levágjuk, de a kijelzőn elfér egy 10-12 karakter)
        String ssid = network.getCurrentSSID();
        display.print(ssid);
        
        // Signal % kiszámolása
        int32_t rssi = network.getRSSI();
        // RSSI: -50 (Kiváló) ... -100 (Rossz)
        int quality = map(rssi, -100, -50, 0, 100);
        if(quality < 0) quality = 0; if(quality > 100) quality = 100;
        
        display.print(" "); display.print(quality); display.print("%");

        // --- 2. SOR: ÓRA BALRA + KIS MÁSODPERC + KÖVETKEZŐ CSENGETÉS ---
        // Óra (HH:MM) - Nagyobb méret (3)
        display.setTextSize(3); 
        display.setCursor(0, 9); // Bal szél, picit lejjebb
        if(t.tm_hour < 10) display.print("0"); display.print(t.tm_hour);
        display.print((millis() % 1000) < 500 ? ":" : " ");
        if(t.tm_min < 10) display.print("0"); display.print(t.tm_min);
        
        // Másodperc (SS) - Kisebb méret (2), közvetlenül utána
        display.setTextSize(2);
        display.print(":");
        if(t.tm_sec < 10) display.print("0"); display.print(t.tm_sec);

        // Jobb felső sarok (ahol a B:ON volt) -> Következő időpont vagy MUTE
        display.setTextSize(1); 
        display.setCursor(95, 0); // Jobb szélre igazítva
        
        if (settings.bellMode == 0) {
            display.print("MUTE");
        } else {
            // Lekérjük a BellManager-től az időpontot (pl. 16:00)
            display.print(bell.getNextEventTimeStr());
        }
    } 
    else {
        display.setCursor(0, 16); display.print("Syncing Time...");
    }
}

void UIManager::drawMenuScreen() {
    display.setTextSize(1); display.setCursor(0,0); 
    if (menuPage == MENU_MAIN) display.print("MAIN MENU");
    else { 
        switch(mainMenuIndex) {
            case 0: display.print("SIGNAL SET"); break;
            case 1: display.print("DISPLAY SET"); break;
            case 2: display.print("STATUS INFO"); break;
            case 3: display.print("SYSTEM"); break;
        }
    }

    display.setTextSize(2); display.setCursor(0,16);
    
    if (menuPage == MENU_MAIN) {
        switch(mainMenuIndex) {
            case 0: display.print("SIGNAL >"); break;
            case 1: display.print("DISPLAY >"); break;
            case 2: display.print("STATUS >"); break;
            case 3: display.print("RESET >"); break;
            case 4: display.print("NETRADIO >"); break;
        }
    } else {
        if (mainMenuIndex == 0) { 
            if(subMenuIndex==0) {
                display.print("BELL: ");
                if(settings.bellMode == 0) display.print("OFF");
                else if(settings.bellMode == 1) display.print("ON");
                else display.print("TODAY");
            }
            if(subMenuIndex==1) display.printf("SOUND:%s", settings.soundEnabled ? "ON" : "OFF");
        }
        else if (mainMenuIndex == 1) { 
            if(subMenuIndex==0) {
                display.print("CLK:");
                if(settings.clockMode == 0) display.print("OFF");
                else if(settings.clockMode == 1) display.print("ON");
                else display.print("FULL");
            }
            if(subMenuIndex==1) display.printf("COUNT:%s", settings.countEnabled ? "ON" : "OFF");
            if(subMenuIndex==2) display.printf("DIMM: %d", settings.dimmLevel);
        }
        else if (mainMenuIndex == 2) drawStatusScreen();
        else if (mainMenuIndex == 3) { if(subMenuIndex==0) display.print("REBOOT ?"); }
    }
}

void UIManager::drawNetRadioScreen() {
    display.setTextSize(1); display.setCursor(0,0); display.print("NetRadio"); 
    struct tm t; if (getLocalTime(&t)) { display.setCursor(90, 0); if(t.tm_hour < 10) display.print("0"); display.print(t.tm_hour); display.print((millis() % 1000) < 500 ? ":" : " "); if(t.tm_min < 10) display.print("0"); display.print(t.tm_min); }
    display.setCursor(0, 12); if (radioList.size() > 0) { display.println(radioList[currentStationIndex].name); } else { display.print("No List"); }
    display.setCursor(0, 24); if (isRadioPlaying) { display.print("PLAYING"); int phase = (millis() / 200) % 5; switch(phase) { case 0: display.print(" ...."); break; case 1: display.print(". ..."); break; case 2: display.print(".. .."); break; case 3: display.print("... ."); break; case 4: display.print(".... "); break; } } else { display.print("BUFFER..."); }
    display.setCursor(90, 24); display.print("VOL "); display.print(audio.getVolume());
}

void UIManager::drawVolumeScreen() { display.setTextSize(1); display.setCursor(0,0); display.print("VOLUME"); display.setTextSize(4); uint8_t vol = audio.getVolume(); int x = (vol == 10) ? 40 : 52; display.setCursor(x, 4); display.print(vol); }
void UIManager::drawSplashScreen() { display.clearDisplay(); display.setTextSize(2); display.setCursor(0,0); display.println("SchoolLive!"); display.setTextSize(1); display.setCursor(0,20); display.println("SmartSpeaker V3.4"); display.display(); }
void UIManager::applyDimming() { uint8_t contrast = 255; if (settings.dimmLevel > 0) { contrast = 255 - (settings.dimmLevel * 50); if (contrast < 5) contrast = 5; } display.ssd1306_command(SSD1306_SETCONTRAST); display.ssd1306_command(contrast); }

void UIManager::drawBootStatus(String status, String details) {
    display.clearDisplay(); display.setTextSize(1); display.setCursor(0, 0); display.print("BOOTING...");
    display.setCursor(0, 12); display.print(status);
    display.setCursor(0, 24); display.print(details);
    display.display();
}