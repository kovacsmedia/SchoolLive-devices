#include <Arduino.h>
#include <LittleFS.h>
#include "Config.h"
#include "AudioManager.h"
#include "NetworkManager.h"
#include "BellManager.h"
#include "UIManager.h"

#include <FS.h>
#include <SD.h>
#include <SD_MMC.h>
#include <WiFi.h>

AudioManager audioManager;
NetworkManager networkManager;
BellManager bellManager(audioManager, networkManager); 
UIManager uiManager(audioManager, networkManager, bellManager);

TaskHandle_t TaskNetworkHandle;

void TaskNetwork(void * pvParameters) {
    networkManager.begin();
    for(;;) {
        networkManager.loop();
        vTaskDelay(100 / portTICK_PERIOD_MS); 
    }
}

void setup() {
    btStop(); 
    Wire.begin(I2C_SDA, I2C_SCL);
    
    // Nincs Serial visszajelzés
    LittleFS.begin(true);

    audioManager.begin();
    uiManager.begin(); 
    bellManager.begin();
    
    // Módosított üzenet
    uiManager.drawBootStatus("Config Check", "Reading config file");
    delay(500);

    // WiFi adatok betöltése és csatlakozás
    bool success = networkManager.syncTimeBlocking();
    
    if (!success) {
        uiManager.drawBootStatus("WIFI FAILED!", "Check wifi.txt");
        delay(3000); 
    } else {
        uiManager.drawBootStatus("WIFI OK!", networkManager.getIP());
        delay(1000);
    }
    
    xTaskCreatePinnedToCore(TaskNetwork, "NetworkTask", 10000, NULL, 1, &TaskNetworkHandle, 0);
}

void loop() {
    audioManager.loop(); 
    uiManager.loop();
    bellManager.loop();    
}