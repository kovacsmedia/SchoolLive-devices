#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// -----------------------------------------------------------------------------
// ESZKÖZ AZONOSÍTÁS
// -----------------------------------------------------------------------------

#define DEVICE_ID "Info 2. terem"

// Ha van gyárilag beégetett / ideiglenes deviceKey, ide írható.
// Normál esetben üres, provisioning után a Preferences tárolja.
#define DEVICE_KEY_DEFAULT ""

// -----------------------------------------------------------------------------
// BACKEND
// -----------------------------------------------------------------------------

#define BACKEND_BASE_URL "https://api.schoollive.hu"

// -----------------------------------------------------------------------------
// HARDVER PINOUT: ESP32-S3 N16R8
// -----------------------------------------------------------------------------

// MAX98357A:
//   BCLK → GPIO14
//   LRC  → GPIO15
//   DIN  → GPIO13
//
// OLED 128x32 I2C:
//   SDA → GPIO21
//   SCL → GPIO47
//
// TTP223:
//   bal  → GPIO12
//   jobb → GPIO6

// --- I2S DAC / MAX98357A ---
#define I2S_BCLK 14
#define I2S_LRC  15
#define I2S_DIN  13

// --- OLED kijelző / I2C ---
#define I2C_SDA 21
#define I2C_SCL 47

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C

// --- ÉRINTŐGOMBOK / GOMBOK ---
#define TOUCH_L 12
#define TOUCH_R 6

// -----------------------------------------------------------------------------
// WIFI
// -----------------------------------------------------------------------------

#define WIFI_SSID "MP"
#define WIFI_PASS "Pw9Bsu79"

// Provisioning/service WiFi.
// A meglévő ProvisioningManager ezeket a neveket várja.
#define PROV_WIFI_SSID WIFI_SSID
#define PROV_WIFI_PASS WIFI_PASS

// -----------------------------------------------------------------------------
// PROVISIONING
// -----------------------------------------------------------------------------

// Mennyi ideig próbálkozzon a provisioning folyamattal.
#define PROV_POLL_TIMEOUT 300000UL     // 5 perc

// Milyen gyakran kérdezze le a provisioning státuszt.
#define PROV_POLL_INTERVAL 3000UL      // 3 mp

// -----------------------------------------------------------------------------
// MQTT / régi kompatibilitási beállítások
// -----------------------------------------------------------------------------

#define MQTT_SERVER "mqtt.schoollive.hu"
#define MQTT_PORT 1883
#define MQTT_USER "student"
#define MQTT_PASS "thesis2025"

// -----------------------------------------------------------------------------
// RENDSZER
// -----------------------------------------------------------------------------

#define WATCHDOG_TIMEOUT_MS 30000
#define NTP_SERVER "pool.ntp.org"
#define SERIAL_BAUD 115200
#define STREAM_TIMEOUT_MS 3000

// -----------------------------------------------------------------------------
// UI IDŐZÍTÉSEK
// -----------------------------------------------------------------------------

#define DOUBLE_CLICK_MS 400
#define LONG_PRESS_MS 1000
#define UI_REFRESH_RATE_MS 250
#define VOLUME_DISPLAY_TIME 2000

// -----------------------------------------------------------------------------
// NETRADIO / régi kompatibilitási beállítás
// -----------------------------------------------------------------------------

#define RADIOLIST_URL "https://schoollive.hu/radiolist.txt"

#endif