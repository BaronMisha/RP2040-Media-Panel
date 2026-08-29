#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

class DisplayManager {
 public:
  bool begin();
  TFT_eSPI& display();
  uint32_t actualSpiClockHz() const;

 private:
  void printSetup();
  void drawInitializationError();

  TFT_eSPI display_;
};
