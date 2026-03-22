#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SyncClient.h – S3.52 Snapcast mód
//
// Változások S3.51-hez képest:
//   • TTS / PLAY_URL: csak UI overlay, audio a Snapcast kezeli → nincs letöltés
//   • BELL: LittleFS-ről lejátszás (offline fallback)
//   • PREPARE: BELL esetén LittleFS ellenőrzés/letöltés, egyébként azonnali ACK
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include "AudioManager.h"
#include "BellManager.h"

#define SYNC_WS_HOST       "api.schoollive.hu"
#define SYNC_WS_PORT       443
#define SYNC_WS_RECONNECT  3000
#define SYNC_PSRAM_BUF_SIZE (256 * 1024)

struct PreparedCmd {
    String commandId;
    String action;      // BELL | TTS | PLAY_URL | STOP_PLAYBACK
    String localPath;   // LittleFS (csak BELL)
    String url;         // eredeti URL (referencia, nem használt audio-ra)
    bool   ready    = false;
};

class SyncClient {
public:
    SyncClient() {}

    void begin(AudioManager& audio, BellManager& bells,
               const String& deviceKey, const String& tenantId = "");
    void loop();

    bool    isConnected()       const { return _connected; }
    int64_t getServerOffsetMs() const { return _serverOffsetMs; }

private:
    AudioManager*    _audio    = nullptr;
    BellManager*     _bells    = nullptr;
    String           _deviceKey;
    WebSocketsClient _ws;
    bool             _connected      = false;
    bool             _wsSetup        = false;
    int64_t          _serverOffsetMs = 0;
    PreparedCmd      _prep;

    void onConnected();
    void onDisconnected();
    void onMessage(const String& msg);

    void handleHello(const JsonDocument& doc);
    void handlePrepare(const JsonDocument& doc);
    void handlePlay(const JsonDocument& doc);
    void handleImmediate(const JsonDocument& doc);

    void sendReadyAck(const String& commandId, uint32_t bufferMs);
    void sendTimeSyncRequest();

    int64_t nowMs() const;
    bool    downloadToLittleFS(const String& url, const String& path);

    static void      wsEventHandler(WStype_t type, uint8_t* payload, size_t length);
    static SyncClient* _instance;
};