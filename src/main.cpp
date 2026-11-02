#include <Arduino.h>

#include "IntercomApp.h"

static intercom::IntercomApp app;

void setup() {
  if (!app.begin()) {
    Serial.println("[main] init failed, halting");
    while (true) {
      delay(1000);
    }
  }
}

void loop() {
  app.loop();
}
