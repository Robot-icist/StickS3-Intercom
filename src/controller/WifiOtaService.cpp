#include "WifiOtaService.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

#include <cstring>

namespace controller {
namespace {

constexpr uint32_t kWifiConnectTimeoutMs = 20000;

}  // namespace

bool WifiOtaService::begin(const char* hostname) {
  if (hostname == nullptr || hostname[0] == '\0') {
    Serial.println("[ota] Missing device hostname; Wi-Fi OTA disabled");
    return false;
  }
  strncpy(_hostname, hostname, sizeof(_hostname) - 1);
  _hostname[sizeof(_hostname) - 1] = '\0';

  if (!loadOrCreateOtaPassword()) {
    Serial.println("[ota] Could not configure authentication; Wi-Fi OTA disabled");
    return false;
  }

  ArduinoOTA.setHostname(_hostname);
  ArduinoOTA.setPassword(_otaPassword.c_str());
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([]() { Serial.println("[ota] Update started"); });
  ArduinoOTA.onEnd(
      []() { Serial.println("\n[ota] Update complete; restarting"); });
  ArduinoOTA.onError(
      [](ota_error_t error) { Serial.printf("[ota] Update failed (%u)\n", error); });

  Preferences preferences;
  if (!preferences.begin("intercom", true)) {
    Serial.println("[wifi] Could not open saved network settings");
    startConfigPortal();
    return true;
  }
  const String ssid = preferences.getString("wifi_ssid", "");
  const String password = preferences.getString("wifi_pass", "");
  preferences.end();

  if (ssid.isEmpty()) {
    startConfigPortal();
    return true;
  }

  WiFi.persistent(false);
  WiFi.setHostname(_hostname);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  _connectStartedAt = millis();
  Serial.printf("[wifi] Connecting to saved network %s\n", ssid.c_str());
  return true;
}

void WifiOtaService::loop() {
  const uint32_t now = millis();
  if (_portalStarted) {
    _dns.processNextRequest();
    _portal.handleClient();
  }

  if (!_otaStarted && WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(_hostname)) {
      _mdnsStarted = true;
      Serial.printf("[mdns] BLE HID controller available at %s.local\n",
                    _hostname);
    } else {
      Serial.println("[mdns] Hostname announcement failed; OTA remains available by IP");
    }
    ArduinoOTA.begin();
    _otaStarted = true;
    Serial.printf("[ota] Ready at %s (%s); firmware and LittleFS OTA enabled\n",
                  _hostname, WiFi.localIP().toString().c_str());
  }

  if (_otaStarted && WiFi.status() != WL_CONNECTED) {
    ArduinoOTA.end();
    _otaStarted = false;
    ArduinoOTA.setPassword(_otaPassword.c_str());
    if (_mdnsStarted) {
      MDNS.end();
      _mdnsStarted = false;
    }
    portENTER_CRITICAL(&_otaSessionMux);
    memset(_otaSessionPassword, 0, sizeof(_otaSessionPassword));
    _otaSessionRequested = false;
    _otaSessionReady = false;
    _otaSessionRestoring = false;
    portEXIT_CRITICAL(&_otaSessionMux);
  }

  if (!_portalStarted && !_otaStarted &&
      WiFi.status() != WL_CONNECTED &&
      millis() - _connectStartedAt >= kWifiConnectTimeoutMs) {
    Serial.println("[wifi] Saved network unavailable; opening setup portal");
    startConfigPortal();
  }

  if (_otaStarted) {
    ArduinoOTA.handle();
    char sessionPassword[sizeof(_otaSessionPassword)] = {};
    bool activateSession = false;
    bool restorePassword = false;
    portENTER_CRITICAL(&_otaSessionMux);
    if (_otaSessionRequested && !_otaSessionReady &&
        !_otaSessionRestoring) {
      memcpy(sessionPassword, _otaSessionPassword, sizeof(sessionPassword));
      activateSession = true;
    } else if (_otaSessionRequested && _otaSessionReady &&
               static_cast<int32_t>(now - _otaSessionExpiresAt) >= 0) {
      _otaSessionRestoring = true;
      restorePassword = true;
    }
    portEXIT_CRITICAL(&_otaSessionMux);

    if (activateSession) {
      ArduinoOTA.end();
      ArduinoOTA.setPassword(sessionPassword);
      ArduinoOTA.begin();
      memset(sessionPassword, 0, sizeof(sessionPassword));
      _otaStarted = true;
      portENTER_CRITICAL(&_otaSessionMux);
      _otaSessionReady = true;
      portEXIT_CRITICAL(&_otaSessionMux);
    } else if (restorePassword) {
      ArduinoOTA.end();
      ArduinoOTA.setPassword(_otaPassword.c_str());
      ArduinoOTA.begin();
      _otaStarted = true;
      portENTER_CRITICAL(&_otaSessionMux);
      memset(_otaSessionPassword, 0, sizeof(_otaSessionPassword));
      _otaSessionRequested = false;
      _otaSessionReady = false;
      _otaSessionRestoring = false;
      portEXIT_CRITICAL(&_otaSessionMux);
    }
  }
}

bool WifiOtaService::createOtaSession(char* output, size_t outputSize) {
  if (output == nullptr || outputSize == 0 ||
      _otaPassword.length() < 8 || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  const IPAddress ip = WiFi.localIP();
  char sessionPassword[sizeof(_otaSessionPassword)] = {};
  portENTER_CRITICAL(&_otaSessionMux);
  if (_otaSessionRestoring) {
    portEXIT_CRITICAL(&_otaSessionMux);
    return false;
  }
  if (!_otaSessionRequested) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < sizeof(_otaSessionPassword) - 1; ++i) {
      _otaSessionPassword[i] = kHex[esp_random() & 0x0F];
    }
    _otaSessionExpiresAt = millis() + 10 * 60 * 1000;
    _otaSessionRequested = true;
    _otaSessionReady = false;
    portEXIT_CRITICAL(&_otaSessionMux);
    return false;
  }
  if (!_otaSessionReady) {
    portEXIT_CRITICAL(&_otaSessionMux);
    return false;
  }
  memcpy(sessionPassword, _otaSessionPassword, sizeof(sessionPassword));
  portEXIT_CRITICAL(&_otaSessionMux);

  const int written = snprintf(
      output, outputSize, "sticks3\n%u.%u.%u.%u\n%s", ip[0], ip[1], ip[2],
      ip[3], sessionPassword);
  memset(sessionPassword, 0, sizeof(sessionPassword));
  return written > 0 && static_cast<size_t>(written) < outputSize;
}

String WifiOtaService::ipAddress() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  return _portalStarted ? WiFi.softAPIP().toString() : String();
}

bool WifiOtaService::loadOrCreateOtaPassword() {
  Preferences preferences;
  if (!preferences.begin("intercom", false)) {
    return false;
  }

  _otaPassword = preferences.getString("ota_pass", "");
  if (_otaPassword.isEmpty()) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    char generatedPassword[25] = {};
    for (size_t i = 0; i < sizeof(generatedPassword) - 1; ++i) {
      generatedPassword[i] = kHex[esp_random() & 0x0F];
    }
    if (preferences.putString("ota_pass", generatedPassword) == 0) {
      preferences.end();
      return false;
    }
    _otaPassword = generatedPassword;
    Serial.printf("[ota] New per-device OTA/setup password: %s (save it now)\n",
                  _otaPassword.c_str());
  }

  preferences.end();
  return _otaPassword.length() >= 8;
}

void WifiOtaService::startConfigPortal() {
  if (_portalStarted) {
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(_hostname);
  char accessPointName[40];
  snprintf(accessPointName, sizeof(accessPointName), "%s-Setup", _hostname);
  
  // Use a simple fixed password for the setup AP, keep OTA password for OTA only
  constexpr const char* kSetupApPassword = "blehid123456";
  if (!WiFi.softAP(accessPointName, kSetupApPassword)) {
    Serial.println("[wifi] Could not start setup access point");
    return;
  }

  const IPAddress portalIp = WiFi.softAPIP();
  if (!_dns.start(53, "*", portalIp)) {
    Serial.println("[wifi] Captive DNS failed; setup remains at http://192.168.4.1/");
  }

  _portal.on("/", HTTP_GET, [this]() {
    _portal.send(
        200, "text/html",
        "<!doctype html><meta name='viewport' content='width=device-width'>"
        "<title>StickS3 Wi-Fi setup</title><h2>StickS3 Wi-Fi setup</h2>"
        "<form method='post' action='/connect'>"
        "<label>Wi-Fi name (SSID)<br><input name='ssid' required></label><br>"
        "<label>Wi-Fi password<br><input name='password' type='password'></label><br>"
        "<button>Save and connect</button></form>");
  });
  _portal.on("/connect", HTTP_POST, [this]() {
    const String ssid = _portal.arg("ssid");
    const String password = _portal.arg("password");
    if (ssid.isEmpty()) {
      _portal.send(400, "text/plain", "Wi-Fi name is required.");
      return;
    }

    Preferences preferences;
    if (!preferences.begin("intercom", false)) {
      _portal.send(500, "text/plain", "Could not save Wi-Fi settings.");
      return;
    }
    const size_t ssidBytes = preferences.putString("wifi_ssid", ssid);
    const size_t passwordBytes = preferences.putString("wifi_pass", password);
    preferences.end();
    if (ssidBytes == 0 || passwordBytes == 0) {
      _portal.send(500, "text/plain", "Could not save Wi-Fi settings.");
      return;
    }

    _portal.send(200, "text/plain",
                 "Wi-Fi settings saved. The controller is restarting.");
    delay(500);
    ESP.restart();
  });
  _portal.onNotFound([this, portalIp]() {
    _portal.sendHeader("Location", String("http://") + portalIp.toString() + "/");
    _portal.send(302, "text/plain", "");
  });
  _portal.begin();
  _portalStarted = true;
  Serial.printf("[wifi] Setup AP %s; password: blehid123456; open http://%s/\n",
                accessPointName, portalIp.toString().c_str());
}

}  // namespace controller
