#pragma once

#include <Arduino.h>

#include "BleMicAudio.h"
#include "BleHidDevice.h"
#include "ControllerFeatures.h"
#include "ControllerUi.h"
#include "WifiOtaService.h"

namespace controller {

class BleHidController {
 public:
  bool begin();
  void loop();

 private:
  BleHidDevice _hid;
  BleMicAudio _micAudio;
  ControllerFeatures _features{_hid, _micAudio};
  ControllerUi _ui;
  WifiOtaService _wifiOta;
  uint32_t _lastRenderAt = 0;
};

}  // namespace controller
