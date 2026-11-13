#pragma once

#include <Arduino.h>

#include "BleHidDevice.h"
#include "BleMicAudio.h"
#include "WifiOtaService.h"

namespace controller {

class WebHidServer {
 public:
  bool begin(BleHidDevice* hid, BleMicAudio* mic);
  void loop(WifiOtaService* wifiOta);

 public:
  bool setupServer();
  void stopServer();
  private:
  bool loadOrCreateCertificate();

 public:
  BleHidDevice* _hid = nullptr;
  BleMicAudio* _mic = nullptr;
  void* _httpsServer = nullptr;  // httpd_handle_t
  volatile bool _running = false;
  volatile bool _serverStartInProgress = false;
  uint32_t _lastServerAttemptMs = 0;
  String _certPem;
  String _keyPem;
};

}  // namespace controller