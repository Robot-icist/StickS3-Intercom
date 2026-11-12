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
  // Keep the speaker configured so M5Unified registers the StickS3 speaker
  // enable callback, then immediately turn the AW8737 PA off. The controller
  // never plays audio, and M5Stack requires the amplifier to be disabled while
  // the shared ES8311 codec is capturing (it otherwise produces idle hiss/
  // whistle). BleMicAudio repeats this before every capture.
  config.internal_spk = true;
  M5.begin(config);
  M5.Speaker.end();
  
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
