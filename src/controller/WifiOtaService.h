#pragma once

#include <Arduino.h>
#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>

namespace controller {

class WifiOtaService {
 public:
  bool begin(const char* hostname);
  void loop();
  bool createOtaSession(char* output, size_t outputSize);
  const char* hostname() const { return _hostname; }
  const char* otaPassword() const { return _otaPassword.c_str(); }
  String ipAddress() const;
  bool otaInProgress() const { return _otaInProgress; }
  uint8_t otaProgress() const { return _otaProgress; }
  const char* otaStatus() const { return _otaStatus; }

 private:
  bool loadOrCreateOtaPassword();
  void startConfigPortal();

  WebServer _portal{80};
  DNSServer _dns;
  char _hostname[32] = {};
  String _otaPassword;
  char _otaSessionPassword[25] = {};
  uint32_t _otaSessionExpiresAt = 0;
  portMUX_TYPE _otaSessionMux = portMUX_INITIALIZER_UNLOCKED;
  bool _otaSessionRequested = false;
  bool _otaSessionReady = false;
  bool _otaSessionRestoring = false;
  uint32_t _connectStartedAt = 0;
  bool _portalStarted = false;
  bool _otaStarted = false;
  bool _mdnsStarted = false;
  bool _otaInProgress = false;
  uint8_t _otaProgress = 0;
  uint32_t _otaCurrent = 0;
  uint32_t _otaTotal = 0;
  char _otaStatus[32] = "READY";
};

}  // namespace controller
