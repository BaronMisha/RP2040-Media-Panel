#include <Arduino.h>

#include "AppConfig.h"
#include "app/AppController.h"
#include "display/DisplayManager.h"
#include "display/UiRenderer.h"

namespace {

DisplayManager displayManager;
UiRenderer uiRenderer(displayManager.display());
AppController app(displayManager, uiRenderer, Serial);

}  // namespace

void setup() {
  Serial.begin(AppConfig::SerialBaud);
  app.begin();
}

void loop() {
  app.update();
}
