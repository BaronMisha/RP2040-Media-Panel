#include "DisplayManager.h"

#include <hardware/clocks.h>
#include <hardware/spi.h>

#include "AppConfig.h"
#include "UiColors.h"

bool DisplayManager::begin() {
  display_.init();
  display_.setRotation(AppConfig::DisplayRotation);
  display_.setSwapBytes(true);

  printSetup();

  if (display_.width() != AppConfig::ScreenWidth ||
      display_.height() != AppConfig::ScreenHeight) {
    Serial.println(F("[ERROR] Expected portrait size 240x320"));
    drawInitializationError();
    return false;
  }

  Serial.println(F("[INFO] Display initialized"));
  return true;
}

TFT_eSPI& DisplayManager::display() {
  return display_;
}

uint32_t DisplayManager::actualSpiClockHz() const {
#if defined(RP2040_PIO_SPI)
  const uint32_t systemClockHz = clock_get_hz(clk_sys);
  constexpr uint32_t PioCyclesPerBit = 2;
  const uint32_t clockDenominator =
      SPI_FREQUENCY * PioCyclesPerBit;
  uint32_t clockDivider =
      (systemClockHz + clockDenominator - 1U) / clockDenominator;

  if (clockDivider < 1U) {
    clockDivider = 1U;
  }

  return systemClockHz / (PioCyclesPerBit * clockDivider);
#else
  return spi_get_baudrate(spi0);
#endif
}

void DisplayManager::printSetup() {
  setup_t setup;
  display_.getSetup(setup);

  Serial.println();
  Serial.println(F("[INFO] RP2040 MEDIA PANEL"));
  Serial.print(F("[INFO] Controller ID: 0x"));
  Serial.println(setup.tft_driver, HEX);
  Serial.print(F("[INFO] System clock: "));
  Serial.print(static_cast<float>(clock_get_hz(clk_sys)) / 1000000.0f, 2);
  Serial.println(F(" MHz"));
  Serial.print(F("[INFO] Display size: "));
  Serial.print(display_.width());
  Serial.print('x');
  Serial.println(display_.height());
#if defined(RP2040_PIO_SPI)
  Serial.print(F("[INFO] PIO SPI clock: "));
#else
  Serial.print(F("[INFO] Hardware SPI clock: "));
#endif
  Serial.print(static_cast<float>(actualSpiClockHz()) / 1000000.0f, 2);
  Serial.println(F(" MHz"));
}

void DisplayManager::drawInitializationError() {
  display_.fillScreen(UiColors::Error);
  display_.setTextFont(2);
  display_.setTextColor(UiColors::PrimaryText, UiColors::Error);
  display_.setCursor(16, 24);
  display_.print(F("DISPLAY SIZE ERROR"));
  display_.setCursor(16, 52);
  display_.print(F("Expected 240x320"));
  display_.setCursor(16, 76);
  display_.print(F("Actual "));
  display_.print(display_.width());
  display_.print('x');
  display_.print(display_.height());
}
