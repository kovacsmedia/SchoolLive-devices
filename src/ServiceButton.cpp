#include "ServiceButton.h"

void ServiceButton::begin(uint8_t pin, uint32_t longPressMs) {
    _pin         = pin;
    _longPressMs = longPressMs;

    // Belső felhúzóellenállás: a gomb csak GND-re húz, külső alkatrész nem kell.
    pinMode(_pin, INPUT_PULLUP);

    _lastRaw      = (digitalRead(_pin) == LOW);
    _stableDown   = _lastRaw;
    _lastChangeMs = millis();
    _pressStartMs = 0;
    _longFired    = false;

    // Ha a gomb már bekapcsoláskor le van nyomva (beragadt, vagy a felhasználó
    // még tartja a gyári visszaállítást kiváltó nyomás után), NEM fegyverezünk
    // – előbb el kell engedni.
    _armed = !_stableDown;

    Serial.printf("[BTN] Szervizgomb GPIO%u (aktiv LOW, belso felhuzas), hosszu nyomas: %lu ms%s\n",
                  (unsigned)_pin, (unsigned long)_longPressMs,
                  _armed ? "" : " – gomb bekapcsolaskor lenyomva, elengedesre var");
}

void ServiceButton::loop() {
    if (_pin == 255) return;

    const bool     raw = (digitalRead(_pin) == LOW);   // aktív alacsony
    const uint32_t now = millis();

    // ── Pergésmentesítés ────────────────────────────────────────────────────
    if (raw != _lastRaw) {
        _lastRaw      = raw;
        _lastChangeMs = now;
        return;
    }
    if (now - _lastChangeMs < DEBOUNCE_MS) return;
    if (raw == _stableDown) {
        // Nincs állapotváltás – de nyomva tartás közben a hosszú nyomást
        // MENET KÖZBEN kell figyelni, nem csak elengedésre.
        if (_stableDown && _armed && !_longFired &&
            (now - _pressStartMs) >= _longPressMs) {
            _longFired = true;
            Serial.printf("[BTN] Hosszu nyomas (%lu ms) – gyari visszaallitas\n",
                          (unsigned long)(now - _pressStartMs));
            if (_onLong) _onLong();
        }
        return;
    }

    // ── Stabil állapotváltás ────────────────────────────────────────────────
    _stableDown = raw;

    if (_stableDown) {
        _pressStartMs = now;
        _longFired    = false;
        if (_armed) Serial.println("[BTN] Lenyomva");
        return;
    }

    // Elengedés
    if (!_armed) {
        // Ez volt a bekapcsoláskori lenyomás vége – mostantól élesben figyelünk.
        _armed = true;
        Serial.println("[BTN] Elengedve – szervizgomb elesitve");
        return;
    }

    if (_longFired) {
        // A hosszú nyomás már elsült, az elengedés nem indít újabb műveletet.
        _longFired = false;
        return;
    }

    Serial.printf("[BTN] Rovid nyomas (%lu ms) – ujrainditas\n",
                  (unsigned long)(now - _pressStartMs));
    if (_onShort) _onShort();
}
