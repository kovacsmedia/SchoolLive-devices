#ifndef SERVICEBUTTON_H
#define SERVICEBUTTON_H

#include <Arduino.h>
#include <functional>

/**
 * Egyetlen szervizgomb kezelése a kijelző nélküli hardver-változathoz.
 *
 * Bekötés: a gomb egyik lába a GPIO-ra, másik a GND-re. Külső ellenállás NEM
 * kell – a láb `INPUT_PULLUP`-ban van, tehát alaphelyzetben HIGH, lenyomva LOW
 * (aktív alacsony).
 *
 * Funkciók:
 *   • rövid nyomás (elengedésre)      → újraindítás
 *   • 7 mp-en túli nyomva tartás      → gyári visszaállítás (aktiválás előtti
 *                                       állapot), majd újraindulás
 *                                       provisioning módban
 *
 * A hosszú nyomás SZÁNDÉKOSAN a 7. másodperc betelésekor sül el, még nyomva
 * tartás közben – így a felhasználó a reagálásból (az eszköz azonnal
 * újraindul) tudja, hogy sikerült, nem kell találgatnia, mikor engedje el.
 *
 * Fegyverzés (`_armed`): a gombot a bekapcsolás után EGYSZER el kell engedni,
 * mielőtt bármelyik funkció elsülhetne. Enélkül egy beragadt gomb, vagy egy
 * gyári visszaállítás után még lenyomva tartott gomb végtelen reset-ciklust
 * okozna.
 */
class ServiceButton {
public:
    using Action = std::function<void()>;

    /** @param pin        a gomb GPIO-ja (aktív alacsony, belső felhúzással)
     *  @param longPressMs ennyi nyomva tartás után sül el a gyári visszaállítás */
    void begin(uint8_t pin, uint32_t longPressMs);

    /** A főciklusból hívandó, ~10-100 ms-onként bőven elég. */
    void loop();

    void onShortPress(Action a)   { _onShort = a; }
    void onLongPress(Action a)    { _onLong  = a; }

private:
    static constexpr uint32_t DEBOUNCE_MS = 50;

    uint8_t  _pin         = 255;
    uint32_t _longPressMs = 7000;

    bool     _armed        = false;  // volt-e már elengedés a bekapcsolás óta
    bool     _stableDown   = false;  // pergésmentesített állapot
    bool     _lastRaw      = false;
    uint32_t _lastChangeMs = 0;
    uint32_t _pressStartMs = 0;
    bool     _longFired    = false;

    Action _onShort;
    Action _onLong;
};

#endif
