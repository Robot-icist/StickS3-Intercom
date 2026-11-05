#include <Arduino.h>
#include <M5Unified.h>

#include "controller/BleHidController.h"

static controller::BleHidController app;

void setup() {
  Serial.begin(115200);
  delay(50);

  auto config = M5.config();
  config.internal_imu = true;
  config.external_imu = true;
  config.internal_mic = true;
  config.internal_spk = true;
  M5.begin(config);

  if (!app.begin()) {
    Serial.println("[controller] init failed, halting");
    while (true) {
      delay(1000);
    }
  }
}

void loop() {
  app.loop();
}
