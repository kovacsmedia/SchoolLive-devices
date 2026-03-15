// ─────────────────────────────────────────────────────────────────────────────
// OtaManager.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "OtaManager.h"
#include "BackendClient.h"
#include "UIManager.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>

void OtaManager::begin(BackendClient& backend, UIManager* ui) {
    _backend = &backend;
    _ui      = ui;

    // Ha az előző boot sikeres volt → confirmmáljuk
    confirmBootIfNeeded();

    // Azonnal ellenőrzünk induláskor (5s késleltetéssel)
    _lastCheckMs = millis() - OTA_CHECK_INTERVAL_MS + 5000;
}

void OtaManager::loop() {
    if (!_backend || !_backend->isReady()) return;
    if (isUpdating()) return;

    unsigned long now = millis();
    if ((now - _lastCheckMs) >= OTA_CHECK_INTERVAL_MS) {
        _lastCheckMs = now;
        checkForUpdate();
    }
}

// ── confirmBootIfNeeded ───────────────────────────────────────────────────────
void OtaManager::confirmBootIfNeeded() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            Serial.println("[OTA] Új firmware – boot megerősítve (rollback törölve)");
            esp_ota_mark_app_valid_cancel_rollback();
            // Sikeres frissítés jelentése a szervernek
            reportStatus(String(FW_VERSION), "SUCCESS", 100);
        }
    }
}

// ── checkForUpdate ────────────────────────────────────────────────────────────
void OtaManager::checkForUpdate() {
    _state = OtaState::CHECKING;
    Serial.printf("[OTA] Verzióellenőrzés... (jelenlegi: %s)\n", FW_VERSION);

    JsonDocument resp;
    int code = 0;
    String path = String(OTA_CHECK_URL) + "?version=" + FW_VERSION + "&deviceClass=SPEAKER&hwModel=" + HW_MODEL;
    bool ok = _backend->getJson(path, resp, code);

    if (!ok || code != 200) {
        Serial.printf("[OTA] Ellenőrzés sikertelen: %d\n", code);
        _state = OtaState::IDLE;
        return;
    }

    bool updateAvailable = resp["updateAvailable"] | false;
    if (!updateAvailable) {
        Serial.println("[OTA] Nincs elérhető frissítés");
        _state = OtaState::IDLE;
        return;
    }

    String version   = resp["latest"]["version"]   | "";
    String url       = resp["latest"]["url"]        | "";
    String sha256    = resp["latest"]["sha256"]     | "";
    size_t sizeBytes = resp["latest"]["sizeBytes"]  | 0;
    bool   mandatory = resp["latest"]["mandatory"]  | false;

    Serial.printf("[OTA] Új verzió: %s (%d bytes) mandatory=%d\n",
                  version.c_str(), sizeBytes, mandatory);

    if (url.isEmpty() || version.isEmpty()) {
        _state = OtaState::IDLE;
        return;
    }

    // Frissítés végrehajtása
    triggerUpdate(url, version, sha256, sizeBytes);
}

// ── triggerUpdate ─────────────────────────────────────────────────────────────
void OtaManager::triggerUpdate(const String& url, const String& version,
                                const String& sha256, size_t sizeBytes) {
    Serial.printf("[OTA] Frissítés indítása: %s\n", version.c_str());
    _pendingVersion = version;

    bool success = performUpdate(url, version, sha256, sizeBytes);

    if (success) {
        Serial.println("[OTA] Frissítés sikeres – újraindítás 3s múlva");
        _state = OtaState::REBOOT_PENDING;
        delay(3000);
        ESP.restart();
    } else {
        Serial.println("[OTA] Frissítés SIKERTELEN");
        _state = OtaState::FAILED;
        reportStatus(version, "FAILED", 0);
    }
}

// ── performUpdate ─────────────────────────────────────────────────────────────
bool OtaManager::performUpdate(const String& url, const String& version,
                                const String& sha256, size_t sizeBytes) {
    reportStatus(version, "DOWNLOADING", 0);
    _state = OtaState::DOWNLOADING;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(30000);

    Serial.printf("[OTA] Letöltés: %s\n", url.c_str());
    if (!http.begin(client, url)) {
        Serial.println("[OTA] HTTP begin sikertelen");
        return false;
    }

    // Device key auth
    String dk = "";
    // BackendClient-ből nem tudjuk közvetlenül kiolvasni, de a header-t beállítjuk
    // A BackendClient addCommonHeaders()-t nem tudjuk hívni, ezért direkt:
    int httpCode = http.GET();
    if (httpCode != 200) {
        Serial.printf("[OTA] HTTP hiba: %d\n", httpCode);
        http.end();
        return false;
    }

    int contentLen = http.getSize();
    if (contentLen <= 0) contentLen = (int)sizeBytes;
    Serial.printf("[OTA] Méret: %d bytes\n", contentLen);

    // OTA partíció meghatározása
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
    if (!update_partition) {
        Serial.println("[OTA] Nincs OTA partíció!");
        http.end();
        return false;
    }
    Serial.printf("[OTA] Írás ide: %s\n", update_partition->label);

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition,
                                   contentLen > 0 ? (size_t)contentLen : OTA_SIZE_UNKNOWN,
                                   &ota_handle);
    if (err != ESP_OK) {
        Serial.printf("[OTA] esp_ota_begin hiba: %s\n", esp_err_to_name(err));
        http.end();
        return false;
    }

    reportStatus(version, "INSTALLING", 0);
    _state = OtaState::INSTALLING;

    // SHA-256 kontex inicializálása
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    // Stream olvasás + OTA írás + SHA számítás
    WiFiClient* stream  = http.getStreamPtr();
    uint8_t buf[1024];
    size_t  written     = 0;
    int     lastProgress = 0;
    unsigned long t0    = millis();

    while (http.connected() && (contentLen < 0 || (int)written < contentLen)) {
        size_t avail = stream->available();
        if (avail == 0) {
            if (millis() - t0 > 15000) {
                Serial.println("[OTA] Stream timeout");
                break;
            }
            delay(1);
            continue;
        }

        size_t toRead = min(avail, sizeof(buf));
        size_t rd     = stream->readBytes(buf, toRead);
        if (rd == 0) continue;

        err = esp_ota_write(ota_handle, buf, rd);
        if (err != ESP_OK) {
            Serial.printf("[OTA] esp_ota_write hiba: %s\n", esp_err_to_name(err));
            break;
        }

        mbedtls_sha256_update(&sha_ctx, buf, rd);
        written += rd;
        t0 = millis();

        // Progress jelentés 10%-onként
        if (contentLen > 0) {
            int progress = (int)((written * 100) / contentLen);
            if (progress - lastProgress >= 10) {
                lastProgress = progress;
                Serial.printf("[OTA] Progress: %d%%\n", progress);
                reportStatus(version, "DOWNLOADING", progress);
            }
        }
    }

    http.end();

    if (contentLen > 0 && (int)written < contentLen) {
        Serial.printf("[OTA] Hiányos letöltés: %d/%d\n", (int)written, contentLen);
        esp_ota_abort(ota_handle);
        return false;
    }

    // SHA-256 ellenőrzés
    if (!sha256.isEmpty()) {
        uint8_t hash[32];
        mbedtls_sha256_finish(&sha_ctx, hash);
        mbedtls_sha256_free(&sha_ctx);

        char hexHash[65] = {};
        for (int i = 0; i < 32; i++) sprintf(hexHash + i*2, "%02x", hash[i]);

        if (sha256 != String(hexHash)) {
            Serial.printf("[OTA] SHA-256 nem egyezik!\n  várt:  %s\n  kapott:%s\n",
                          sha256.c_str(), hexHash);
            esp_ota_abort(ota_handle);
            return false;
        }
        Serial.println("[OTA] SHA-256 OK ✓");
    }

    // OTA befejezés
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        Serial.printf("[OTA] esp_ota_end hiba: %s\n", esp_err_to_name(err));
        return false;
    }

    // Boot partíció beállítása
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        Serial.printf("[OTA] set_boot_partition hiba: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.printf("[OTA] ✅ %s (%d bytes) – boot partíció beállítva\n",
                  version.c_str(), (int)written);
    reportStatus(version, "INSTALLING", 100);
    return true;
}

// ── reportStatus ──────────────────────────────────────────────────────────────
void OtaManager::reportStatus(const String& version, const String& status,
                               int progress, const String& error) {
    if (!_backend) return;
    JsonDocument req;
    req["version"]  = version;
    req["status"]   = status;
    req["progress"] = progress;
    if (!error.isEmpty()) req["error"] = error;

    JsonDocument resp;
    int code = 0;
    _backend->postJson(OTA_REPORT_URL, req, resp, code);
}