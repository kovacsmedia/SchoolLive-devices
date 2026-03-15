#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// OtaManager.h – ESP32-S3 OTA frissítés rollback védelemmel
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"

#define OTA_CHECK_INTERVAL_MS  (60 * 60 * 1000UL)  // óránként
#define OTA_REPORT_URL         "/firmware/ota-status"
#define OTA_CHECK_URL          "/firmware/check"

enum class OtaState {
  IDLE,
  CHECKING,
  DOWNLOADING,
  INSTALLING,
  REBOOT_PENDING,
  FAILED,
};

class BackendClient;
class UIManager;

class OtaManager {
public:
  OtaManager() {}
  void begin(BackendClient& backend, UIManager* ui = nullptr);
  void loop();

  // SyncEngine OTA_UPDATE parancsra azonnali frissítés
  void triggerUpdate(const String& url, const String& version,
                     const String& sha256, size_t sizeBytes);

  bool isUpdating() const { return _state != OtaState::IDLE &&
                                    _state != OtaState::FAILED; }

  // Boot után hívandó – jelzi a szervernek a sikeres frissítést
  void confirmBootIfNeeded();

private:
  BackendClient* _backend = nullptr;
  UIManager*     _ui      = nullptr;

  OtaState      _state         = OtaState::IDLE;
  unsigned long _lastCheckMs   = 0;
  String        _pendingVersion;

  void checkForUpdate();
  bool performUpdate(const String& url, const String& version,
                     const String& sha256, size_t sizeBytes);
  void reportStatus(const String& version, const String& status,
                    int progress = 0, const String& error = "");
};