#include "BackendClient.h"
#include <WiFiClient.h>  // Arduino-ESP32 v3.x: WiFiClient typedef alias for NetworkClient

void BackendClient::begin(const String& baseUrl) {
    _baseUrl = baseUrl;

    if (_baseUrl.endsWith("/")) {
        _baseUrl.remove(_baseUrl.length() - 1);
    }
}

void BackendClient::setDeviceKey(const String& deviceKey) {
    _deviceKey = deviceKey;
}

bool BackendClient::isReady() const {
    return _baseUrl.length() > 0 && _deviceKey.length() > 0;
}

void BackendClient::addCommonHeaders(HTTPClient& http) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-device-key", _deviceKey);
}

void BackendClient::waitCooldown() {
    if (_lastHttpEndMs == 0) return;

    unsigned long elapsed = millis() - _lastHttpEndMs;

    if (elapsed < HTTP_COOLDOWN_MS) {
        delay(HTTP_COOLDOWN_MS - elapsed);
    }
}

// ---------------------------------------------------------------------------
// POST JSON
// ---------------------------------------------------------------------------

bool BackendClient::postJson(
    const String& path,
    const JsonDocument& req,
    JsonDocument& resp,
    int& httpCode
) {
    if (!isReady()) return false;

    waitCooldown();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(8000);

    const String url = _baseUrl + path;

    Serial.printf("[HTTP] POST %s\n", url.c_str());

    if (!http.begin(client, url)) {
        Serial.println("[HTTP] begin() failed");
        return false;
    }

    addCommonHeaders(http);

    String body;
    serializeJson(req, body);

    httpCode = http.POST(body);

    Serial.printf("[HTTP] httpCode: %d\n", httpCode);

    if (httpCode <= 0) {
        Serial.printf("[HTTP] Error: %s\n", http.errorToString(httpCode).c_str());

        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    String responseStr = http.getString();

    Serial.printf("[HTTP] Response: %.300s\n", responseStr.c_str());

    _lastHttpEndMs = millis();
    http.end();

    DeserializationError err = deserializeJson(resp, responseStr);

    if (err) {
        Serial.printf("[HTTP] JSON parse error: %s\n", err.c_str());
        return false;
    }

    return httpCode >= 200 && httpCode < 300;
}

// ---------------------------------------------------------------------------
// GET JSON
// ---------------------------------------------------------------------------

bool BackendClient::getJson(
    const String& path,
    JsonDocument& resp,
    int& httpCode
) {
    if (!isReady()) return false;

    waitCooldown();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(8000);

    const String url = _baseUrl + path;

    Serial.printf("[HTTP] GET %s\n", url.c_str());

    if (!http.begin(client, url)) {
        Serial.println("[HTTP] GET begin() failed");
        return false;
    }

    addCommonHeaders(http);

    httpCode = http.GET();

    Serial.printf("[HTTP] httpCode: %d\n", httpCode);

    if (httpCode <= 0) {
        Serial.printf("[HTTP] Error: %s\n", http.errorToString(httpCode).c_str());

        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    String responseStr = http.getString();

    _lastHttpEndMs = millis();
    http.end();

    DeserializationError err = deserializeJson(resp, responseStr);

    if (err) {
        Serial.printf("[HTTP] GET JSON parse error: %s\n", err.c_str());
        return false;
    }

    return httpCode >= 200 && httpCode < 300;
}

// ---------------------------------------------------------------------------
// downloadFile – hangfájl letöltése LittleFS-re
// ---------------------------------------------------------------------------

// Hány menetben próbáljuk összeszedni a fájlt, és mennyit várunk két menet
// között. Gyenge vonalon a letöltés darabokban jön össze (ld. downloadRange).
static const int          DL_MAX_ATTEMPTS   = 4;
static const unsigned int DL_RETRY_DELAY_MS = 1500;

bool BackendClient::downloadFile(
    const String& url,
    const String& localPath,
    size_t expectedBytes
) {
    if (LittleFS.exists(localPath)) {
        if (expectedBytes == 0) {
            Serial.printf("[DL] Already exists: %s (skip)\n", localPath.c_str());
            return true;
        }

        File f = LittleFS.open(localPath, "r");

        if (f) {
            size_t existingSize = f.size();
            f.close();

            if (existingSize == expectedBytes) {
                Serial.printf(
                    "[DL] Already exists: %s (%d bytes, skip)\n",
                    localPath.c_str(),
                    existingSize
                );
                return true;
            }

            Serial.printf(
                "[DL] Size mismatch %s: local=%d expected=%d, re-downloading\n",
                localPath.c_str(),
                existingSize,
                expectedBytes
            );
        }
    }

    /*
     * EGY RÉGI, ROSSZ MÉRETŰ PÉLDÁNY NEM FOLYTATHATÓ.
     *
     * A fenti ág csak akkor engedett idáig, ha a helyi fájl mérete NEM egyezik
     * a várttal – az viszont MÁS TARTALOM (a hang cserélve lett a szerveren),
     * nem egy félbemaradt letöltés. Rátoldani a folytatást kevert, játszhatatlan
     * fájlt adna. A folytatás ezért kizárólag EZEN a híváson belül érvényes,
     * ahol tudjuk, hogy a részleges bájtok ugyanerről az URL-ről jöttek.
     */
    if (LittleFS.exists(localPath)) {
        LittleFS.remove(localPath);
    }

    String fullUrl = url;

    if (!fullUrl.startsWith("http://") && !fullUrl.startsWith("https://")) {
        if (!fullUrl.startsWith("/")) {
            fullUrl = "/" + fullUrl;
        }

        fullUrl = _baseUrl + fullUrl;

        Serial.printf("[DL] Resolved relative URL → %s\n", fullUrl.c_str());
    }

    /*
     * TÖBB PRÓBÁLKOZÁS, FOLYTATÁSSAL.
     *
     * MIÉRT: gyenge WiFi mellett (-85 dBm körül) egy 400 kB-os hang egyetlen
     * TLS-kapcsolaton gyakran NEM ér végig – a stream megáll, a 15 mp-es
     * várakozás lejár, és eddig ilyenkor a fél fájl a kukába ment. A
     * következő szinkron megint nulláról indult, megint elakadt: az a hang
     * SOHA nem került fel az eszközre. Egy néma csengetés viszont nem fordulhat
     * elő, ezért amit már letöltöttünk, azt megtartjuk, és `Range` fejléccel
     * onnan folytatjuk. A szerver támogatja (`Accept-Ranges: bytes`), így egy
     * akadozó vonalon is összeáll a fájl – csak több menetben.
     */
    size_t haveBytes = 0;
    bool   complete  = false;

    for (int attempt = 1; attempt <= DL_MAX_ATTEMPTS && !complete; attempt++) {
        if (attempt > 1) {
            Serial.printf("[DL] Ujraprobalkozas %d/%d – eddig %u B van meg\n",
                          attempt, DL_MAX_ATTEMPTS, (unsigned)haveBytes);
            delay(DL_RETRY_DELAY_MS);
        }

        const size_t before = haveBytes;

        complete = downloadRange(fullUrl, localPath, expectedBytes, haveBytes);

        /*
         * NULLA HALADÁS: a kapcsolat nem áll össze (a szerver nem elérhető, a
         * WiFi kiesett). Az újrapróbálkozásnak csak akkor van értelme, ha
         * legalább pár bájt átjött – különben csak a szinkront tartjuk fel.
         */
        if (!complete && haveBytes <= before && attempt > 1) {
            Serial.println("[DL] Nincs haladas – feladjuk");
            break;
        }
    }

    if (!complete) {
        Serial.printf("[DL] Sikertelen: %s (%u / %u B)\n",
                      localPath.c_str(), (unsigned)haveBytes, (unsigned)expectedBytes);

        /*
         * A FÉLKÉSZ FÁJLT ELDOBJUK – de csak azt.
         *
         * Egy csonka hang kattanva, félbeszakadva szólna, ami rosszabb, mint
         * a gyári defaultra esni. A teljes, csak "más méretű" fájl ide már nem
         * jut el (ld. a `done` feltételt a downloadRange-ben).
         */
        LittleFS.remove(localPath);
        return false;
    }

    Serial.printf("[DL] Done: %s (%u bytes)\n", localPath.c_str(), (unsigned)haveBytes);
    return true;
}

// ---------------------------------------------------------------------------
// downloadRange – EGY letöltési menet, `haveBytes` bájttól folytatva
//
// Visszatérés: true, ha a fájl a menet végére teljes lett.
// A `haveBytes` mindig a lemezen ténylegesen meglévő bájtszámra frissül, akkor
// is, ha a menet félbeszakadt – erre épül a következő menet folytatása.
// ---------------------------------------------------------------------------

bool BackendClient::downloadRange(
    const String& fullUrl,
    const String& localPath,
    size_t expectedBytes,
    size_t& haveBytes
) {
    waitCooldown();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(15000);

    if (haveBytes == 0) {
        Serial.printf("[DL] Downloading %s → %s\n", fullUrl.c_str(), localPath.c_str());
    } else {
        Serial.printf("[DL] Folytatas %u B-tol: %s\n", (unsigned)haveBytes, localPath.c_str());
    }

    if (!http.begin(client, fullUrl)) {
        Serial.println("[DL] begin() failed");
        return false;
    }

    if (_deviceKey.length() > 0) {
        http.addHeader("x-device-key", _deviceKey);
    }

    if (haveBytes > 0) {
        http.addHeader("Range", "bytes=" + String((unsigned)haveBytes) + "-");
    }

    int httpCode = http.GET();

    Serial.printf("[DL] httpCode: %d\n", httpCode);

    /*
     * 206 = a szerver elfogadta a Range-et, a törzs a folytatás.
     * 200 Range-kéréssel = a szerver FIGYELMEN KÍVÜL hagyta, és az EGÉSZ fájlt
     *     küldi – ilyenkor a meglévő részt el kell dobni, különben duplán
     *     írnánk az elejét.
     */
    const bool resuming = (haveBytes > 0 && httpCode == 206);

    if (httpCode != 200 && httpCode != 206) {
        Serial.printf("[DL] Error: %s\n", http.errorToString(httpCode).c_str());

        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    if (haveBytes > 0 && !resuming) {
        Serial.println("[DL] A szerver nem tamogatja a folytatast – ujrakezdes 0-tol");
        haveBytes = 0;
    }

    WiFiClient* stream = http.getStreamPtr();

    if (!stream) {
        Serial.println("[DL] No stream");

        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    // Hely-ellenőrzés a letöltés ELŐTT. A "Cannot open for write" önmagában
    // nem árulja el, mi a baj – tele van a fájlrendszer, túl hosszú a név,
    // vagy elfogytak a fájl-leírók. Írjuk ki a tényeket.
    const size_t fsTotal = LittleFS.totalBytes();
    const size_t fsUsed  = LittleFS.usedBytes();
    const size_t fsFree  = (fsTotal > fsUsed) ? (fsTotal - fsUsed) : 0;

    // Folytatásnál már csak a HÁTRALÉVŐ rész helye kell.
    const size_t needBytes = (expectedBytes > haveBytes) ? (expectedBytes - haveBytes) : 0;

    if (needBytes > 0 && fsFree < needBytes + 4096) {
        Serial.printf("[DL] ⛔ NINCS ELEG HELY: kell %u B, szabad %u B (osszes %u, hasznalt %u) – %s\n",
                      (unsigned)needBytes, (unsigned)fsFree,
                      (unsigned)fsTotal, (unsigned)fsUsed, localPath.c_str());
        _lastHttpEndMs = millis();
        http.end();
        return false;
    }

    File file = LittleFS.open(localPath, resuming ? "a" : "w");

    if (!file) {
        Serial.printf("[DL] Cannot open for write: %s (nevhossz=%u, szabad=%u B, osszes=%u B, hasznalt=%u B)\n",
                      localPath.c_str(), (unsigned)localPath.length(),
                      (unsigned)fsFree, (unsigned)fsTotal, (unsigned)fsUsed);
        Serial.println("[DL] Tipp: a LittleFS nevhossz-korlat 64 karakter, es a partíció-geometria "
                       "eltérése is okozhatja – ilyenkor 'pio run -t uploadfs' ujraformaz.");

        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    uint8_t buf[512];
    size_t  written      = 0;             // EBBEN a menetben írt bájtok
    int     contentLength = http.getSize();  // a menet törzsének hossza
    unsigned long dlStart = millis();

    // Miért állt le a ciklus – a naplóban ez különbözteti meg a néma
    // vonalat (stall) a bontott kapcsolattól.
    const char* stopReason = "kesz";
    bool        clean      = true;   // igaz, amíg nem szakadt meg rendellenesen

    while (contentLength != 0) {
        if (!http.connected() && stream->available() == 0) {
            stopReason = "kapcsolat bontva";
            clean      = false;
            break;
        }

        size_t avail = stream->available();

        if (avail == 0) {
            if (millis() - dlStart > 15000) {
                stopReason = "idotullepes (nem jott adat 15 mp-ig)";
                clean      = false;
                break;
            }

            delay(1);
            continue;
        }

        size_t toRead = min(avail, sizeof(buf));
        size_t read   = stream->readBytes(buf, toRead);

        if (read > 0) {
            /*
             * AZ ÍRÁS EREDMÉNYÉT MEG KELL NÉZNI. Tele fájlrendszernél a
             * `write()` kevesebbet ír – eddig ezt is átvitt bájtnak számoltuk,
             * és a hiba a hálózatra lett fogva.
             */
            size_t wrote = file.write(buf, read);

            written    += wrote;
            haveBytes  += wrote;

            if (wrote != read) {
                stopReason = "lemezre iras hibaja (megtelt a fajlrendszer?)";
                clean      = false;
                break;
            }

            dlStart = millis();
        }

        if (contentLength > 0 && (int)written >= contentLength) {
            break;
        }
    }

    file.close();

    _lastHttpEndMs = millis();
    http.end();

    /*
     * A SZERVER A MÉRVADÓ, NEM A NYILVÁNTARTOTT MÉRET.
     *
     * Eddig `haveBytes == expectedBytes` döntött. Az `expectedBytes` viszont
     * ADATBÁZIS-METAADAT, ami elavulhat a lemezen lévő fájlhoz képest – és
     * akkor egy HIBÁTLANUL, teljes egészében letöltött fájlt minősítettünk
     * hibásnak, majd a hívó törölte. A gyári csengetőhangnál ez pontosan a
     * tiltott kimenetel: az eszközön nem maradt default hang.
     *
     * (Élesben megtörtént: a szerver 107 448 bájtot küldött, az adatbázis
     * 106 870-et mondott, a letöltés tökéletes volt, mégis kukába ment. A
     * folytatás-kísérlet ezután 416-ot kapott, hiszen nem volt mit folytatni.)
     *
     * Mostantól a HTTP-válasz dönt: ha a törzs végigjött és a kapcsolat
     * rendben zárult, a fájl teljes. Az eltérő nyilvántartás csak figyelmeztetés.
     */
    const bool gotWholeBody = (contentLength > 0)
                                ? ((int)written >= contentLength)
                                : (written > 0 && clean);
    const bool done = clean && gotWholeBody && haveBytes > 0;

    if (done && expectedBytes > 0 && haveBytes != expectedBytes) {
        Serial.printf("[DL] ⚠ Meret-elteres a nyilvantartashoz kepest: kaptunk %u B, "
                      "a szerver listaja %u B-t mondott – a FAJL MEGTARTVA\n",
                      (unsigned)haveBytes, (unsigned)expectedBytes);
    }

    if (!done) {
        Serial.printf("[DL] Megszakadt: %u / %u B – %s\n",
                      (unsigned)haveBytes,
                      (unsigned)(contentLength > 0 ? (size_t)contentLength : expectedBytes),
                      stopReason);
    }

    return done;
}

// ---------------------------------------------------------------------------
// setSnapConfig – WS HELLO / BEACON_ACK után hívja a DeviceAgent
// ---------------------------------------------------------------------------

void BackendClient::setSnapConfig(const String& host, uint16_t port, const String& deviceId) {
    _snapHost       = host;
    _snapPort       = port;
    _deviceId       = deviceId;
    _snapConfigValid = host.length() > 0 && port > 0 && deviceId.length() > 0;
    Serial.printf("[SNAPCFG] set → deviceId=%s host=%s port=%u\n",
                  deviceId.c_str(), host.c_str(), port);
}

// ---------------------------------------------------------------------------
// postJsonUnauthed – provisioning
// ---------------------------------------------------------------------------

bool BackendClient::postJsonUnauthed(
    const String& path,
    const JsonDocument& req,
    JsonDocument& resp,
    int& httpCode
) {
    if (_baseUrl.length() == 0) return false;

    waitCooldown();

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(7000);

    const String url = _baseUrl + path;

    if (!http.begin(client, url)) {
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    String body;
    serializeJson(req, body);

    httpCode = http.POST(body);

    if (httpCode <= 0) {
        _lastHttpEndMs = millis();
        http.end();

        return false;
    }

    String responseStr = http.getString();

    _lastHttpEndMs = millis();
    http.end();

    DeserializationError err = deserializeJson(resp, responseStr);

    if (err) {
        return false;
    }

    return httpCode >= 200 && httpCode < 300;
}

// ---------------------------------------------------------------------------
// confirmProvisioning
// ---------------------------------------------------------------------------

bool BackendClient::confirmProvisioning(
    const String& provisioningToken,
    String& outDeviceKey,
    String& outWifiSsid,
    String& outWifiPass,
    String& outTenantId
) {
    outDeviceKey = "";
    outWifiSsid = "";
    outWifiPass = "";
    outTenantId = "";

    JsonDocument req;
    req["provisioningToken"] = provisioningToken;

    JsonDocument resp;
    int code = 0;

    bool ok = postJsonUnauthed(
        "/provision/provision/confirm",
        req,
        resp,
        code
    );

    if (!ok) {
        return false;
    }

    if (resp["deviceKey"].is<const char*>()) {
        outDeviceKey = resp["deviceKey"].as<const char*>();
    }

    if (resp["wifi"]["ssid"].is<const char*>()) {
        outWifiSsid = resp["wifi"]["ssid"].as<const char*>();
    }

    if (resp["wifi"]["password"].is<const char*>()) {
        outWifiPass = resp["wifi"]["password"].as<const char*>();
    }

    // Multi-node cluster: a device.tenantId a válaszban (ha a backend már
    // ezt a mezőt is küldi – devices.provision.routes.ts). Hiánya nem hiba,
    // csak a node-discovery marad kihasználatlan ezen az eszközön.
    if (resp["device"]["tenantId"].is<const char*>()) {
        outTenantId = resp["device"]["tenantId"].as<const char*>();
    }

    return outDeviceKey.length() > 0;
}

// ---------------------------------------------------------------------------
// Multi-node cluster: node discovery
// ---------------------------------------------------------------------------

bool BackendClient::locateNode(const String& tenantId, String& outHostname) {
    outHostname = "";
    if (tenantId.length() == 0) return false;

    JsonDocument resp;
    int code = 0;

    bool ok = getJson("/cluster/locate?tenantId=" + tenantId, resp, code);
    if (!ok) return false;

    if (resp["hostname"].is<const char*>()) {
        outHostname = resp["hostname"].as<const char*>();
    }

    return outHostname.length() > 0;
}

// ---------------------------------------------------------------------------
// OTA — firmware verziócheck
// ---------------------------------------------------------------------------

bool BackendClient::checkFirmware(
    const String& currentVersion,
    const String& deviceClass,
    const String& hwModel,
    FirmwareCheckResult& outResult
) {
    outResult = FirmwareCheckResult();

    if (!isReady()) return false;

    // Query paraméterek URL-encode-olása minimális (csak alfanumerikus értékek
    // várhatók itt - verzió string mint "S4.4", deviceClass "SPEAKER", hwModel "ESP32_S3").
    String path = "/firmware/check?version=" + currentVersion
                + "&deviceClass=" + deviceClass;
    if (hwModel.length() > 0) {
        path += "&hwModel=" + hwModel;
    }

    JsonDocument resp;
    int code = 0;
    if (!getJson(path, resp, code)) {
        return false;
    }

    outResult.updateAvailable = resp["updateAvailable"] | false;

    if (outResult.updateAvailable && resp["latest"].is<JsonObject>()) {
        JsonObject latest = resp["latest"];
        outResult.version   = latest["version"]   | "";
        outResult.url       = latest["url"]       | "";
        outResult.sizeBytes = latest["sizeBytes"] | 0;
        outResult.sha256    = latest["sha256"]    | "";
        outResult.mandatory = latest["mandatory"] | false;
        outResult.notes     = latest["notes"]     | "";
    }

    return true;
}

// ---------------------------------------------------------------------------
// OTA — folyamatos státuszjelentés a backendre
// ---------------------------------------------------------------------------

bool BackendClient::reportOtaStatus(
    const String& version,
    const String& status,
    int progress,
    const String& errorMsg
) {
    if (!isReady()) return false;

    JsonDocument req;
    req["version"]  = version;
    req["status"]   = status;
    req["progress"] = progress;
    if (errorMsg.length() > 0) {
        req["error"] = errorMsg;
    }

    JsonDocument resp;
    int code = 0;
    return postJson("/firmware/ota-status", req, resp, code);
}