#pragma once
// SyncClient.h – SchoolLive S3.54
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include "AudioManager.h"
#include "BellManager.h"
#include "Config.h"

class UIManager;
class SnapcastClient;  // forward declaration

struct PreparedCmd {
    String commandId;
    String action;
    String url;
    String localPath;
    bool   ready = false;
};

class SyncClient {
public:
    SyncClient() {}

    void begin(AudioManager& audio, BellManager& bells,
               const String& deviceKey, const String& tenantId);
    void loop();

    void setUIManager(UIManager* ui)           { _ui = ui; }
    void setSnapClient(SnapcastClient* snap)   { _snap = snap; }
    void setOnPlayAction(void (*cb)(const String&)) { _onPlayAction = cb; }

    bool isConnected() const { return _connected; }

private:
    static SyncClient* _instance;
    static void wsEventHandler(WStype_t type, uint8_t* payload, size_t length);

    WebSocketsClient _ws;
    AudioManager*    _audio     = nullptr;
    BellManager*     _bells     = nullptr;
    UIManager*       _ui        = nullptr;
    SnapcastClient*  _snap      = nullptr;
    String           _deviceKey;
    bool             _connected = false;
    bool             _wsSetup   = false;
    int64_t          _serverOffsetMs = 0;
    PreparedCmd      _prep;

    void (*_onPlayAction)(const String&) = nullptr;

    void onConnected();
    void onDisconnected();
    void onMessage(const String& msg);

    void handleHello(const JsonDocument& doc);
    void handlePrepare(const JsonDocument& doc);
    void handlePlay(const JsonDocument& doc);
    void handleImmediate(const JsonDocument& doc);

    void    sendReadyAck(const String& commandId, uint32_t bufferMs);
    void    sendTimeSyncRequest();
    int64_t nowMs() const;
    bool    downloadToLittleFS(const String& url, const String& path);
    void    _notifyActivity();
};