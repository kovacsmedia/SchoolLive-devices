#pragma once
#include <Arduino.h>

class PersistStore;
class UIManager;

// BLE provisioning ki van vezetve (méretcsökkentés + új beüzemelési modell miatt).
// Ez egy kompatibilis stub, hogy a projektben megmaradhasson a fájl.
class ProvisioningBLE {
public:
  void begin(PersistStore& /*store*/, UIManager& /*ui*/) {}
  void loop() {}
  bool isActive() const { return false; }
};