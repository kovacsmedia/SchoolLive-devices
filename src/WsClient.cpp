#include "WsClient.h"

void WsClient::begin(const String& host, uint16_t port, const String& deviceKey) {
    _host      = host;
    _port      = port;
    _deviceKey = deviceKey;
    _started   = true;

    String path = "/sync?deviceKey=" + deviceKey;

    _ws.beginSSL(host.c_str(), port, path.c_str());
    _ws.setExtraHeaders(("x-device-key: " + deviceKey).c_str());
    _ws.setReconnectInterval(_reconnectIntervalMs);

    _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
        wsEvent(type, payload, length);
    });

    Serial.printf("[WS] begin → wss://%s:%d%s\n", host.c_str(), port, path.c_str());
}

void WsClient::loop() {
    if (!_started) return;
    _ws.loop();
}

bool WsClient::sendJson(const JsonDocument& doc) {
    if (!_connected) return false;
    String s;
    serializeJson(doc, s);
    bool ok = _ws.sendTXT(s);
    if (!ok) Serial.println("[WS] sendTXT failed");
    return ok;
}

void WsClient::wsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            _connected = true;
            _reconnectIntervalMs = 1000UL;
            _ws.setReconnectInterval(_reconnectIntervalMs);
            Serial.println("[WS] ✅ Csatlakozva a szerverhez");
            break;

        case WStype_DISCONNECTED:
            if (_connected) {
                Serial.println("[WS] ⚠️ Lecsatlakozva a szerverről");
            }
            _connected = false;
            _disconnectedAtMs = millis();
            // Exponenciális backoff
            _reconnectIntervalMs = min(_reconnectIntervalMs * 2, MAX_RECONNECT_MS);
            _ws.setReconnectInterval(_reconnectIntervalMs);
            Serial.printf("[WS] Következő újracsatlakozás: %lums\n", _reconnectIntervalMs);
            break;

        case WStype_TEXT:
            if (_msgCb && payload && length > 0) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, (const char*)payload, length);
                if (!err) {
                    _msgCb(doc);
                } else {
                    Serial.printf("[WS] JSON parse hiba: %s\n", err.c_str());
                }
            }
            break;

        case WStype_ERROR:
            Serial.println("[WS] Error esemény");
            break;

        case WStype_PING:
        case WStype_PONG:
            break;

        default:
            break;
    }
}
