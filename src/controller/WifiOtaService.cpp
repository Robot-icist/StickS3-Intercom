#include "WifiOtaService.h"

#include <ArduinoOTA.h>
#include <M5Unified.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>

#include <cstring>

namespace controller {
namespace {

constexpr uint32_t kWifiConnectTimeoutMs = 20000;

static uint8_t s_otaPercent = 255;
static int s_otaBarFill = 0;
static char s_lastOtaStatus[32] = "";

void beginOtaProgressScreen(const char *status) {
  auto &d = M5.Display;
  const int w = d.width();
  const int h = d.height();
  d.fillScreen(0x0000);
  d.setTextDatum(lgfx::textdatum_t::top_center);
  d.setFont(&fonts::Font2);
  d.setTextColor(0xF81F, 0x0000);
  d.drawString("OTA UPDATE", w / 2, h / 2 - 54);
  d.setFont(&fonts::Font0);
  d.setTextColor(0xFFFF, 0x0000);
  d.drawString(status, w / 2, h / 2 - 30);
  const int bx = 16;
  const int by = h / 2 - 6;
  const int bw = w - 32;
  const int bh = 12;
  d.drawRect(bx, by, bw, bh, 0xFFFF);
  d.setTextColor(0xFFE0, 0x0000);
  d.drawString("DO NOT POWER OFF", w / 2, by + 34);
  char percentText[16];
  snprintf(percentText, sizeof(percentText), "0%%");
  d.setTextColor(0xFFFF, 0x0000);
  d.drawString(percentText, w / 2, by + 16);
  s_otaPercent = 0;
  s_otaBarFill = 0;
  M5.Display.waitDisplay();
  strncpy(s_lastOtaStatus, status, sizeof(s_lastOtaStatus) - 1);
  s_lastOtaStatus[sizeof(s_lastOtaStatus) - 1] = '\0';
}

void updateOtaStatusText(const char *status) {
  if (strcmp(s_lastOtaStatus, status) == 0) {
    return;
  }
  auto &d = M5.Display;
  const int w = d.width();
  const int h = d.height();
  d.fillRect(w / 2 - 70, h / 2 - 32, 140, 12, 0x0000);
  d.setTextDatum(lgfx::textdatum_t::top_center);
  d.setFont(&fonts::Font0);
  d.setTextColor(0xFFFF, 0x0000);
  d.drawString(status, w / 2, h / 2 - 30);
  d.waitDisplay();
  strncpy(s_lastOtaStatus, status, sizeof(s_lastOtaStatus) - 1);
  s_lastOtaStatus[sizeof(s_lastOtaStatus) - 1] = '\0';
}

void updateOtaProgressBar(uint8_t percent) {
  if (percent == s_otaPercent) {
    return;
  }
  auto &d = M5.Display;
  const int w = d.width();
  const int h = d.height();
  const int bx = 16;
  const int by = h / 2 - 6;
  const int bw = w - 32;
  const int bh = 12;
  const int interiorWidth = bw - 2;
  const int newFill = interiorWidth * static_cast<int>(percent) / 100;
  if (newFill > s_otaBarFill) {
    d.fillRect(bx + 1 + s_otaBarFill, by + 1, newFill - s_otaBarFill,
               bh - 2, 0x07E0);
  }
  d.setTextDatum(lgfx::textdatum_t::top_center);
  d.setFont(&fonts::Font0);
  d.fillRect(w / 2 - 24, by + 16, 48, 12, 0x0000);
  d.setTextColor(0xFFFF, 0x0000);
  char percentText[16];
  snprintf(percentText, sizeof(percentText), "%u%%",
           static_cast<unsigned>(percent));
  d.drawString(percentText, w / 2, by + 16);
  s_otaPercent = percent;
  s_otaBarFill = newFill;
  M5.Display.waitDisplay();
}

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
  ArduinoOTA.onStart([this]() {
    _otaInProgress = true;
    _otaProgress = 0;
    _otaCurrent = 0;
    _otaTotal = 0;
    strncpy(_otaStatus, "STARTING", sizeof(_otaStatus) - 1);
    _otaStatus[sizeof(_otaStatus) - 1] = '\0';
    beginOtaProgressScreen(_otaStatus);
    Serial.println("[ota] Update started");
  });
  ArduinoOTA.onProgress([this](unsigned int current, unsigned int total) {
    _otaCurrent = current;
    _otaTotal = total;
    if (total > 0) {
      _otaProgress = static_cast<uint8_t>((static_cast<uint64_t>(current) * 100U) / total);
    } else {
      _otaProgress = 0;
    }
    strncpy(_otaStatus, "UPDATING", sizeof(_otaStatus) - 1);
    _otaStatus[sizeof(_otaStatus) - 1] = '\0';
    updateOtaStatusText(_otaStatus);
    updateOtaProgressBar(_otaProgress);
    Serial.printf("[ota] Progress: %u%%\r", _otaProgress);
  });
  ArduinoOTA.onEnd([this]() {
    _otaProgress = 100;
    _otaInProgress = false;
    strncpy(_otaStatus, "COMPLETE", sizeof(_otaStatus) - 1);
    _otaStatus[sizeof(_otaStatus) - 1] = '\0';
    beginOtaProgressScreen(_otaStatus);
    updateOtaProgressBar(100);
    Serial.println("\n[ota] Update complete; restarting");
  });
  ArduinoOTA.onError([this](ota_error_t error) {
    _otaInProgress = false;
    strncpy(_otaStatus, "FAILED", sizeof(_otaStatus) - 1);
    _otaStatus[sizeof(_otaStatus) - 1] = '\0';
    beginOtaProgressScreen(_otaStatus);
    updateOtaProgressBar(_otaProgress);
    Serial.printf("[ota] Update failed (%u)\n", error);
  });

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

  // Main page with network scanning and click-to-prefill
  _portal.on("/", HTTP_GET, [this]() {
    _portal.send(
        200, "text/html",
        "<!doctype html>"
        "<html>"
        "<head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>StickS3 Wi-Fi Setup</title>"
        "<style>"
        "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; max-width: 400px; margin: 2rem auto; padding: 1rem; background: #fafafa; }"
        "h1 { color: #333; font-size: 1.5rem; margin-bottom: 0.5rem; }"
        ".subtitle { color: #666; margin-bottom: 1.5rem; font-size: 0.9rem; }"
        ".form-group { margin-bottom: 1rem; background: white; padding: 1rem; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }"
        "label { display: block; margin-bottom: 0.5rem; font-weight: 600; font-size: 0.9rem; color: #333; }"
        "input { width: 100%; padding: 0.75rem; font-size: 1rem; box-sizing: border-box; border: 1px solid #ddd; border-radius: 4px; }"
        "input:focus { outline: none; border-color: #007bff; box-shadow: 0 0 0 2px rgba(0,123,255,0.2); }"
        "button { width: 100%; padding: 1rem; font-size: 1.1rem; font-weight: 600; background: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; }"
        "button:disabled { background: #ccc; cursor: not-allowed; }"
        "button:hover:not(:disabled) { background: #0056b3; }"
        ".networks { max-height: 200px; overflow-y: auto; border: 1px solid #ddd; border-radius: 4px; background: white; }"
        ".network-item { padding: 0.75rem 1rem; cursor: pointer; border-bottom: 1px solid #eee; display: flex; justify-content: space-between; align-items: center; }"
        ".network-item:last-child { border-bottom: none; }"
        ".network-item:hover { background: #f5f5f5; }"
        ".network-item.selected { background: #e7f1ff; }"
        ".network-info { display: flex; flex-direction: column; }"
        ".network-ssid { font-weight: 500; color: #333; }"
        ".network-meta { font-size: 0.75rem; color: #666; margin-top: 0.25rem; }"
        ".signal { font-size: 0.75rem; color: #666; white-space: nowrap; margin-left: 0.5rem; }"
        ".lock { color: #007bff; }"
        ".unlock { color: #28a745; }"
        "#status { margin-top: 1rem; text-align: center; font-size: 0.9rem; min-height: 1.2em; }"
        ".success { color: #28a745; }"
        ".error { color: #dc3545; }"
        ".info { color: #007bff; }"
        "</style>"
        "</head>"
        "<body>"
        "<h1>StickS3 Wi-Fi Setup</h1>"
        "<p class='subtitle'>Select or enter WiFi network:</p>"
        "<div class='form-group'>"
        "<label>Available Networks</label>"
        "<div class='networks' id='networks'>Scanning...</div>"
        "</div>"
        "<div class='form-group'>"
        "<label>SSID</label>"
        "<input type='text' id='ssid' placeholder='Network name' autocomplete='off'>"
        "</div>"
        "<div class='form-group'>"
        "<label>Password</label>"
        "<input type='password' id='pass' placeholder='Password (leave empty for open)' autocomplete='off'>"
        "</div>"
        "<button id='connectBtn' onclick='connect()'>Connect</button>"
        "<p id='status'></p>"

        "<script>"
        "let selectedSsid = '';"
        "async function scan() {"
        "  const res = await fetch('/scan');"
        "  const nets = await res.json();"
        "  const div = document.getElementById('networks');"
        "  if (nets.length === 0) {"
        "    div.innerHTML = '<div class=\"network-item\" style=\"color:#999;text-align:center\">No networks found</div>';"
        "    return;"
        "  }"
        "  div.innerHTML = nets.map(function(n) {"
        "    var ssidEscaped = n.ssid.replace(/\\\\/g, '\\\\\\\\').replace(/'/g, \"\\\\'\");"
        "    return '<div class=\"network-item\" onclick=\"selectNet(\\'' + ssidEscaped + '\\', ' + n.rssi + ', ' + n.enc + ')\">'"
        "  + '<div class=\"network-info\">'"
        "  + '<span class=\"network-ssid\">' + n.ssid + '</span>'"
        "  + '<span class=\"network-meta\">' + n.rssi + ' dBm ' + (n.enc ? '<span class=\"lock\">&#128274;</span>' : '<span class=\"unlock\">&#128275;</span>') + '</span>'"
        "  + '</div>'"
        "  + '<span class=\"signal\">' + signalBars(n.rssi) + '</span>'"
        "  + '</div>';"
        "  }).join('');"
        "}"
        "function signalBars(rssi) {"
        "  if (rssi >= -50) return '▂▄▆█';"
        "  if (rssi >= -60) return '▂▄▆_';"
        "  if (rssi >= -70) return '▂▄__';"
        "  return '▂___';"
        "}"
        "function selectNet(ssid, rssi, enc) {"
        "  selectedSsid = ssid;"
        "  document.getElementById('ssid').value = ssid;"
        "  document.getElementById('pass').focus();"
        "  document.querySelectorAll('.network-item').forEach(function(el) { el.classList.remove('selected'); });"
        "  event.currentTarget.classList.add('selected');"
        "}"
        "async function connect() {"
        "  const btn = document.getElementById('connectBtn');"
        "  const status = document.getElementById('status');"
        "  const ssid = document.getElementById('ssid').value.trim();"
        "  const pass = document.getElementById('pass').value;"
        "  if (!ssid) {"
        "    status.innerHTML = '<span class=\"error\">Please enter or select a network</span>';"
        "    return;"
        "  }"
        "  btn.disabled = true;"
        "  btn.textContent = 'Connecting...';"
        "  status.innerHTML = '<span class=\"info\">Connecting to ' + ssid + '...</span>';"
        "  try {"
        "    const res = await fetch('/connect', {"
        "      method: 'POST',"
        "      headers: {'Content-Type': 'application/json'},"
        "      body: JSON.stringify({ssid: ssid, pass: pass})"
        "    });"
        "    const result = await res.json();"
        "    if (result.success) {"
        "      status.innerHTML = '<span class=\"success\">Connected! Device will restart...</span>';"
        "      setTimeout(function() { location.reload(); }, 3000);"
        "    } else {"
        "      status.innerHTML = '<span class=\"error\">Failed: ' + (result.error || 'Unknown error') + '</span>';"
        "      btn.disabled = false;"
        "      btn.textContent = 'Connect';"
        "    }"
        "  } catch (e) {"
        "    status.innerHTML = '<span class=\"error\">Error: ' + e.message + '</span>';"
        "    btn.disabled = false;"
        "    btn.textContent = 'Connect';"
        "  }"
        "}"
        "scan();"
        "setInterval(scan, 10000);"
        "</script>"
        "</body>"
        "</html>");
  });

  // Scan endpoint - returns JSON array of networks
  _portal.on("/scan", HTTP_GET, [this]() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      _portal.send(200, "application/json", "[]");
      return;
    }
    if (n == 0) {
      WiFi.scanNetworks(true);  // Start async scan
      _portal.send(200, "application/json", "[]");
      return;
    }
    
    String json = "[";
    for (int i = 0; i < n; ++i) {
      if (i > 0) json += ",";
      String ssid = WiFi.SSID(i);
      ssid.replace("\\", "\\\\");
      ssid.replace("\"", "\\\"");
      json += "{\"ssid\":\"" + ssid + "\",";
      json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      json += "\"enc\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN) + "}";
    }
    json += "]";
    WiFi.scanDelete();
    WiFi.scanNetworks(true);  // Start next async scan
    _portal.send(200, "application/json", json);
  });

  // Connect endpoint - accepts JSON, saves credentials and connects
  _portal.on("/connect", HTTP_POST, [this]() {
    if (!_portal.hasArg("plain")) {
      _portal.send(400, "application/json", "{\"success\":false,\"error\":\"No data\"}");
      return;
    }
    
    String body = _portal.arg("plain");
    String ssid, pass;
    
    // Simple JSON parse — use the same format as IntercomTransport
    int ssidStart = body.indexOf("\"ssid\":\"");
    if (ssidStart >= 0) {
      ssidStart += 8;  // skip past "\"ssid\":\""
      int ssidEnd = body.indexOf("\"", ssidStart);
      ssid = body.substring(ssidStart, ssidEnd);
    }
    int passStart = body.indexOf("\"pass\":\"");
    if (passStart >= 0) {
      passStart += 8;  // skip past "\"pass\":\""
      int passEnd = body.indexOf("\"", passStart);
      pass = body.substring(passStart, passEnd);
    }
    
    if (ssid.length() == 0) {
      _portal.send(400, "application/json", "{\"success\":false,\"error\":\"Empty SSID\"}");
      return;
    }
    
    _portal.send(200, "application/json", "{\"success\":true}");
    
    // Save and connect after response sent
    delay(500);
    Preferences preferences;
    if (!preferences.begin("intercom", false)) {
      Serial.println("[wifi] Could not save Wi-Fi settings");
      return;
    }
    const size_t ssidBytes = preferences.putString("wifi_ssid", ssid);
    const size_t passBytes = preferences.putString("wifi_pass", pass);
    preferences.end();
    
    if (ssidBytes == 0 || passBytes == 0) {
      Serial.println("[wifi] Could not save Wi-Fi settings");
      return;
    }
    
    Serial.printf("[wifi] Connecting to %s\n", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    
    uint32_t start = millis();
    while (millis() - start < 20000) {
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[wifi] Connected to %s, IP: %s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
        delay(500);
        ESP.restart();
        return;
      }
      delay(100);
    }
    Serial.printf("[wifi] Connection to %s timed out\n", ssid.c_str());
    // If connection fails, the portal stays active for retry
  });

  // Captive portal redirects
  _portal.onNotFound([this, portalIp]() {
    _portal.sendHeader("Location", String("http://") + portalIp.toString() + "/");
    _portal.send(302, "text/plain", "");
  });
  _portal.begin();
  _portalStarted = true;
  Serial.printf("[wifi] Setup AP %s; password: blehid123456; open http://%s/\n",
                accessPointName, portalIp.toString().c_str());
  
  // Start initial async scan
  WiFi.scanNetworks(true);
}

}  // namespace controller
