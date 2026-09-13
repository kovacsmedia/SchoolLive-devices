#include "WsClient.h"
#include "PsramJson.h"

void WsClient::begin(const String& host, uint16_t port, const String& deviceKey) {
    _host      = host;
    _port      = port;
    _deviceKey = deviceKey;
    _started   = true;

    String path = "/sync?deviceKey=" + deviceKey;

    _ws.beginSSL(host.c_str(), port, path.c_str());
    _ws.setExtraHeaders(("x-device-key: " + deviceKey).c_str());
    _ws.setReconnectInterval(_reconnectIntervalMs);

    /*
     * Heartbeat: 10 mp-enként ping, 3 mp pong-türelem, 2 kihagyás után
     * bontás. Enélkül egy elhalt TCP-t csak a következő íráskor vettünk
     * észre (mérve ~70 mp) – addig az eszköz online-nak hitte magát.
     */
    _ws.enableHeartbeat(10000, 3000, 2);

    _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
        wsEvent(type, payload, length);
    });

    Serial.printf("[WS] begin → wss://%s:%d%s\n", host.c_str(), port, path.c_str());
}

void WsClient::loop() {
    if (!_started) return;
    _ws.loop();

    // Élőjel kiértékelése. Átmenetnél naplózunk, hogy a logból látszódjon,
    // MIÉRT vált offline-ra a csengetés-logika.
    if (_connected) {
        const bool alive =
            (millis() - _lastRxMs) <= WS_SILENCE_TIMEOUT_MS;
        if (alive != _linkAlive) {
            _linkAlive = alive;
            Serial.printf("[WS] %s (utolso valasz %lu ms)\n",
                          alive ? "✅ eloejel rendben"
                                : "⚠️ nincs valasz a szervertol – offline modra valtunk",
                          (unsigned long)(millis() - _lastRxMs));
        }
    }

    // A relocate callback-et SZÁNDÉKOSAN itt, a `_ws.loop()` visszatérése UTÁN
    // hívjuk (nem a wsEvent()-en belül) – onnan hívva a callback (ami újra
    // meghívja `begin()`-t, azaz `_ws.beginSSL()`-t) még a `_ws.loop()` saját
    // hívási vermén belülről módosítaná a `_ws` belső állapotát.
    if (_relocatePending) {
        _relocatePending = false;
        _consecutiveFailures = 0;
        if (_relocateCb) _relocateCb();
    }
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
            _lastRxMs  = millis();
            _linkAlive = true;
            _reconnectIntervalMs = 1000UL;
            _ws.setReconnectInterval(_reconnectIntervalMs);
            _consecutiveFailures = 0;
            Serial.println("[WS] ✅ Csatlakozva a szerverhez");
            break;

        case WStype_DISCONNECTED:
            if (_connected) {
                Serial.println("[WS] ⚠️ Lecsatlakozva a szerverről");
            }
            _connected = false;
            _linkAlive = false;
            _disconnectedAtMs = millis();
            // Exponenciális backoff
            _reconnectIntervalMs = min(_reconnectIntervalMs * 2, MAX_RECONNECT_MS);
            _ws.setReconnectInterval(_reconnectIntervalMs);
            Serial.printf("[WS] Következő újracsatlakozás: %lums\n", _reconnectIntervalMs);

            _consecutiveFailures++;
            if (_consecutiveFailures >= RELOCATE_AFTER_FAILURES) {
                _relocatePending = true;
            }
            break;

        case WStype_TEXT:
            _lastRxMs = millis();
            if (_msgCb && payload && length > 0) {
                auto handle = [&](JsonDocument& doc) {
                    DeserializationError err =
                        deserializeJson(doc, (const char*)payload, length);
                    if (!err) {
                        _msgCb(doc);
                    } else {
                        Serial.printf("[WS] JSON parse hiba: %s\n", err.c_str());
                    }
                };

                // A NAGY üzeneteket (SCHEDULE_SYNC: teljes napi rend +
                // hanglista) PSRAM-ban dolgozzuk fel – azok tíz kB-os fát
                // építenek a szűkös belső DRAM-ban. A kicsiket (PREPARE,
                // PLAY, BEACON_ACK) SZÁNDÉKOSAN a gyorsabb belső RAM-ban
                // hagyjuk: azok a csengetés időzítési útján vannak, és a
                // PSRAM lassabb elérésű.
                if (length > WS_PSRAM_PARSE_THRESHOLD) {
                    PSRAM_JSON_DOC(doc);
                    handle(doc);
                } else {
                    JsonDocument doc;
                    handle(doc);
                }
            }
            break;

        case WStype_ERROR:
            Serial.println("[WS] Error esemény");
            break;

        case WStype_PING:
        case WStype_PONG:
            // A pong a legfontosabb élőjel: akkor is jön, ha a backendnek
            // éppen semmi mondanivalója nincs.
            _lastRxMs = millis();
            break;

        default:
            break;
    }
}
