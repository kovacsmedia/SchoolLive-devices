#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>

// --- PROVISIONING ---
#define PROV_WIFI_SSID     "MP"
#define PROV_WIFI_PASS     "Pw9Bsu79"
#define PROV_POLL_INTERVAL 10000   // ms
#define PROV_POLL_TIMEOUT  600000  // 10 perc, utána újraindul

// --- HARDVER PINOUT ---
#define I2S_BCLK 14
#define I2S_LRC  15
#define I2S_DIN  13
#define I2C_SDA 21
#define I2C_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C
#define TOUCH_L 12
#define TOUCH_R 27

// --- HÁLÓZAT ---
#define MQTT_SERVER "mqtt.schoollive.hu"
#define MQTT_PORT 1883
#define MQTT_USER "student"
#define MQTT_PASS "thesis2025"

// --- RENDSZER ---
#define WATCHDOG_TIMEOUT_MS 30000
#define NTP_SERVER "pool.ntp.org"
#define SERIAL_BAUD 115200
#define STREAM_TIMEOUT_MS 3000

// --- UI ---
#define DOUBLE_CLICK_MS 400
#define LONG_PRESS_MS 1000
#define UI_REFRESH_RATE_MS 250
#define VOLUME_DISPLAY_TIME 2000

// --- BACKEND ---
#define BACKEND_BASE_URL "https://api.schoollive.hu"  // ← JAVÍTVA

// --- DEVICE ---
#define DEVICE_KEY_DEFAULT ""
#define FW_VERSION "3.5"

#endif