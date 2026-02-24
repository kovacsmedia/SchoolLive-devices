#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- ESZKÖZ AZONOSÍTÁS ---
// Ezt később majd dinamikussá tesszük (pl. MAC cím alapján), 
// de most legyen egy fix ID a fejlesztéshez.
#define DEVICE_ID "Info 2. terem"

// --- HARDVER PINOUT (ESP-WROOM-32) ---
// I2S DAC (MAX98357A)
#define I2S_BCLK 14
#define I2S_LRC  15
#define I2S_DIN  13

// OLED (I2C)
#define I2C_SDA 21
#define I2C_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C

// ÉRINTŐGOMBOK
#define TOUCH_L 12
#define TOUCH_R 27

// --- HÁLÓZAT ---
// Ideiglenes adatok, később a NetworkManager kezeli majd
#define WIFI_SSID "MP"
#define WIFI_PASS "Pw9Bsu79"

#define MQTT_SERVER "mqtt.schoollive.hu"
#define MQTT_PORT 1883
#define MQTT_USER "student"      // Ha van auth
#define MQTT_PASS "thesis2025"   // Ha van auth

// --- RENDSZER ---
#define WATCHDOG_TIMEOUT_MS 30000
#define NTP_SERVER "pool.ntp.org"
#define SERIAL_BAUD 115200
#define STREAM_TIMEOUT_MS 3000

// --- UI IDŐZÍTÉSEK ---
#define DOUBLE_CLICK_MS 400
#define LONG_PRESS_MS 1000     // 1 mp hosszú nyomás a menühöz
#define UI_REFRESH_RATE_MS 250 // Negyed másodpercenként frissítjük az órát
#define VOLUME_DISPLAY_TIME 2000 // Hány ms-ig mutassa a hangerőt
// --- NetRadio ---
#define RADIOLIST_URL "https://schoollive.hu/radiolist.txt"
// --- BACKEND ---
// Példa: "https://api.schoollive.hu" vagy "https://schoollive.hu/api" attól függ, nálad hol fut.
// (A backend route-ok rooton vannak: /devices, /provision, /health) :contentReference[oaicite:3]{index=3}
#define BACKEND_BASE_URL "https://schoollive.hu"

// DEV teszthez (később NVS-ből jön, provisioning adja)
#define DEVICE_KEY_DEFAULT ""
#define FW_VERSION "3.15"
#endif