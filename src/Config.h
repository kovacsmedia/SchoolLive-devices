#ifndef CONFIG_H
#define CONFIG_H
#include <Arduino.h>

// ============================================================
//  SchoolLive ESP32-S3-N16R8 konfiguráció
//  Flash: 16MB  |  PSRAM: 8MB OSPI
// ============================================================

// --- PROVISIONING ---
#define PROV_WIFI_SSID     "MP"
#define PROV_WIFI_PASS     "Pw9Bsu79"
#define PROV_POLL_INTERVAL 10000   // ms
#define PROV_POLL_TIMEOUT  600000  // 10 perc, utána újraindul

// --- HARDVER PINOUT (ESP32-S3) ---
// I2S DAC (MAX98357A)
#define I2S_BCLK 14
#define I2S_LRC  15
#define I2S_DIN  13

// OLED (I2C) – SCL: 47 (GPIO22 nincs S3-on)
#define I2C_SDA  21
#define I2C_SCL  47
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  32
#define OLED_ADDR     0x3C

// TTP223 érintőgombok (digitális bemenet)
#define TOUCH_L 12
#define TOUCH_R  6   // GPIO27 nincs S3-on

// --- PSRAM ---
// A PSRAM automatikusan elérhető heap-ként ha BOARD_HAS_PSRAM definiált.
// Nagy bufferekhez (audio stream, HTTP) használd a ps_malloc()-ot:
//   uint8_t* buf = (uint8_t*) ps_malloc(AUDIO_BUF_SIZE);
#define AUDIO_BUF_SIZE   (64 * 1024)   // 64KB stream buffer PSRAM-ban
#define HTTP_BUF_SIZE    (32 * 1024)   // 32KB HTTP válasz buffer

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
#define DOUBLE_CLICK_MS   400
#define LONG_PRESS_MS    1000
#define UI_REFRESH_RATE_MS 250
#define VOLUME_DISPLAY_TIME 2000

// --- BACKEND ---
#define BACKEND_BASE_URL "https://api.schoollive.hu"

// --- DEVICE ---
#define DEVICE_KEY_DEFAULT ""
#define FW_VERSION "S3.4"

#endif