#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SyncClient.h – SyncCast protokoll ESP32-S3-N16R8-hoz
//
// Kapcsolódik a backend WebSocket szerverhez (wss://api.schoollive.hu/sync).
// PREPARE → hangfájl előkészítése (LittleFS-ről vagy letöltés PSRAM-ba)
// READY ACK → szerver értesítése
// PLAY → precíz lejátszás gettimeofday() + esp_timer alapján
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include "AudioManager.h"
#include "BellManager.h"

#define SYNC_WS_HOST        "api.schoollive.hu"
#define SYNC_WS_PORT        443
#define SYNC_WS_RECONNECT   3000   // ms
#define SYNC_PREP_TIMEOUT   1800   // ms – ennyi idő a prefetchre
#define SYNC_PSRAM_BUF_SIZE (256 * 1024)  // 256KB TTS buffer PSRAM-ban

// Előkészített parancs állapota
struct PreparedCmd {
    String commandId;
    String action;        // BELL | TTS | PLAY_URL | STOP_PLAYBACK
    String localPath;     // LittleFS elérési út (bell, TTS)
    String url;           // stream URL (PLAY_URL)
    bool   ready    = false;
    bool   fromPsram = false;
    uint8_t* psramBuf = nullptr;
    size_t   psramLen = 0;
};

class SyncClient {
public:
    SyncClient() {}

    void begin(AudioManager& audio, BellManager& bells,
               const String& deviceKey, const String& tenantId);
    void loop();   // Call from network task ~every 10ms

    bool isConnected() const { return _connected; }

    // Szerver UTC időbélyeg offset (ms) – gettimeofday() kiegészítője
    int64_t getServerOffsetMs() const { return _serverOffsetMs; }

private:
    AudioManager*  _audio    = nullptr;
    BellManager*   _bells    = nullptr;
    String         _deviceKey;
    String         _tenantId;

    WebSocketsClient _ws;
    bool             _connected    = false;
    bool             _wsSetup      = false;
    int64_t          _serverOffsetMs = 0;
    unsigned long    _lastPingMs   = 0;

    // Pending prepare – csak egy egyszerre
    PreparedCmd  _prep;
    uint8_t*     _psramBuf = nullptr;  // statikus PSRAM foglalás

    // ── Handlers ────────────────────────────────────────────────────────────
    void onConnected();
    void onDisconnected();
    void onMessage(const String& msg);

    void handleHello(const JsonDocument& doc);
    void handlePrepare(const JsonDocument& doc);
    void handlePlay(const JsonDocument& doc);
    void handleImmediate(const JsonDocument& doc);

    void sendReadyAck(const String& commandId, uint32_t bufferMs);
    void sendTimeSyncRequest();

    // ── Helpers ─────────────────────────────────────────────────────────────
    int64_t nowMs() const;   // NTP-szinkronizált ms (gettimeofday)
    bool    downloadToPsram(const String& url);
    bool    downloadToLittleFS(const String& url, const String& path);

    static void wsEventHandler(WStype_t type, uint8_t* payload, size_t length);
    static SyncClient* _instance;
};