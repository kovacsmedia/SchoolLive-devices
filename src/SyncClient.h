// SyncClient.h – SchoolLive S3.54
// Változások S3.52 → S3.54:
//   • setUIManager(UIManager*) – WS küldés/fogadás előtt setNetActivity() híváshoz

#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include "AudioManager.h"
#include "BellManager.h"
#include "Config.h"

// Forward deklaráció – körkörös include elkerülése
class UIManager;

struct PreparedCmd {
    String commandId  = "";
    String action     = "";
    String url        = "";
    String localPath  = "";
    bool   ready      = false;
};

class SyncClient {
public:
    static SyncClient* _instance;

    void begin(AudioManager& audio, BellManager& bells,
               const String& deviceKey, const String& tenantId);
    void loop();

    bool isConnected() const { return _connected; }

    /**
     * Opcionális UIManager regisztráció.
     * WS küldés és fogadás előtt setNetActivity()-t hív.
     */
    void setUIManager(UIManager* ui) { _ui = ui; }

    /** Callback: "RADIO" / "UZENET" / "" → UIManager.setPlayingState() */
    void setOnPlayAction(std::function<void(const String&)> cb) { _onPlayAction = cb; }

private:
    WebSocketsClient _ws;
    AudioManager*    _audio     = nullptr;
    BellManager*     _bells     = nullptr;
    UIManager*       _ui        = nullptr;
    String           _deviceKey = "";
    bool             _connected = false;
    bool             _wsSetup   = false;

    int64_t          _serverOffsetMs = 0;
    PreparedCmd      _prep;

    std::function<void(const String&)> _onPlayAction;

    static void wsEventHandler(WStype_t type, uint8_t* payload, size_t length);

    void onConnected();
    void onDisconnected();
    void onMessage(const String& msg);

    void handleHello(const JsonDocument& doc);
    void handlePrepare(const JsonDocument& doc);
    void handlePlay(const JsonDocument& doc);
    void handleImmediate(const JsonDocument& doc);

    void    sendReadyAck(const String& commandId, uint32_t bufferMs);
    void    sendTimeSyncRequest();
    bool    downloadToLittleFS(const String& url, const String& path);
    int64_t nowMs() const;

    void _notifyActivity();   // _ui->setNetActivity() ha _ui != nullptr
};