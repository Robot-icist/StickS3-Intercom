#include "../WebHidServer.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp_http_server.h>
#include <esp_https_server.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform.h>
#include <mbedtls/x509_crt.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace controller {
namespace {

constexpr char kTlsNs[] = "webhidtls";

void *tlsCalloc(size_t c, size_t s) { return calloc(c, s); }
void tlsFree(void *p) { free(p); }

bool configureAllocator() {
  return mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree) == 0;
}

struct CertCtx {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context rng;
  mbedtls_pk_context key;
  mbedtls_x509write_cert crt;
  CertCtx() {
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&rng);
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
  }
  ~CertCtx() {
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
  }
};

bool genCert(const std::string &host, const IPAddress &ip, std::string &cert,
             std::string &key, int &err) {
  CertCtx ctx;
  static const unsigned char pers[] = "WebHID HTTPS";
  err = mbedtls_ctr_drbg_seed(&ctx.rng, mbedtls_entropy_func, &ctx.entropy,
                               pers, sizeof(pers) - 1);
  if (err) return false;
  err = mbedtls_pk_setup(&ctx.key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  if (err) return false;
  err = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(ctx.key),
                            mbedtls_ctr_drbg_random, &ctx.rng);
  if (err) return false;
  mbedtls_x509write_crt_set_version(&ctx.crt, MBEDTLS_X509_CRT_VERSION_3);
  mbedtls_x509write_crt_set_md_alg(&ctx.crt, MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&ctx.crt, &ctx.key);
  mbedtls_x509write_crt_set_issuer_key(&ctx.crt, &ctx.key);
  std::string dn = "CN=" + host + ",O=StickS3,C=US";
  err = mbedtls_x509write_crt_set_subject_name(&ctx.crt, dn.c_str());
  if (err) return false;
  err = mbedtls_x509write_crt_set_issuer_name(&ctx.crt, dn.c_str());
  if (err) return false;
  err = mbedtls_x509write_crt_set_validity(&ctx.crt, "20260101000000",
                                           "20360101000000");
  if (err) return false;
  err = mbedtls_x509write_crt_set_basic_constraints(&ctx.crt, 0, -1);
  if (err) return false;
  err = mbedtls_x509write_crt_set_key_usage(&ctx.crt,
                                            MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
  if (err) return false;

  std::array<unsigned char, 128> san{};
  bool hasIp = ip != IPAddress(0, 0, 0, 0);
  size_t dnsLen = host.length();
  size_t seqLen = dnsLen + 2 + (hasIp ? 6 : 0);
  if (dnsLen == 0 || seqLen >= 128) {
    err = MBEDTLS_ERR_X509_BAD_INPUT_DATA;
    return false;
  }
  size_t sanLen = 0;
  san[sanLen++] = 0x30;
  san[sanLen++] = (unsigned char)seqLen;
  san[sanLen++] = 0x82;
  san[sanLen++] = (unsigned char)dnsLen;
  memcpy(san.data() + sanLen, host.data(), dnsLen);
  sanLen += dnsLen;
  if (hasIp) {
    san[sanLen++] = 0x87;
    san[sanLen++] = 4;
    for (size_t i = 0; i < 4; ++i) san[sanLen++] = ip[i];
  }
  err = mbedtls_x509write_crt_set_extension(&ctx.crt, MBEDTLS_OID_SUBJECT_ALT_NAME,
                                            MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME),
                                            0, san.data(), sanLen);
  if (err) return false;

  std::array<unsigned char, 16> serial{};
  err = mbedtls_ctr_drbg_random(&ctx.rng, serial.data(), serial.size());
  if (err) return false;
  serial[0] &= 0x7F;
  serial[0] |= 0x01;
  err = mbedtls_x509write_crt_set_serial_raw(&ctx.crt, serial.data(),
                                              serial.size());
  if (err) return false;

  std::array<unsigned char, 2048> certBuf{};
  std::array<unsigned char, 1024> keyBuf{};
  err = mbedtls_x509write_crt_pem(&ctx.crt, certBuf.data(), certBuf.size(),
                                  mbedtls_ctr_drbg_random, &ctx.rng);
  if (err) return false;
  err = mbedtls_pk_write_key_pem(&ctx.key, keyBuf.data(), keyBuf.size());
  if (err) return false;

  cert.assign(reinterpret_cast<const char *>(certBuf.data()));
  key.assign(reinterpret_cast<const char *>(keyBuf.data()));
  return true;
}

bool loadOrCreateCert(const std::string &host, const IPAddress &ip,
                      String &cert, String &key) {
  Preferences prefs;
  if (!prefs.begin(kTlsNs, false)) return false;
  std::string identity = host + "|" + ip.toString().c_str();
  String savedId = prefs.getString("identity", "");
  String savedCert = prefs.getString("certificate", "");
  String savedKey = prefs.getString("private_key", "");
  if (savedId.equals(identity.c_str()) && savedCert.length() &&
      savedKey.length()) {
    cert = savedCert;
    key = savedKey;
    prefs.end();
    return true;
  }
  int err = 0;
  std::string c, k;
  if (!genCert(host, ip, c, k, err)) {
    prefs.end();
    return false;
  }
  bool saved = prefs.putString("identity", identity.c_str()) > 0 &&
               prefs.putString("certificate", c.c_str()) > 0 &&
               prefs.putString("private_key", k.c_str()) > 0;
  prefs.end();
  if (!saved) return false;
  cert = c.c_str();
  key = k.c_str();
  return true;
}

// JSON helpers
String extractJsonString(const char *json, const char *key) {
  char qk[32];
  snprintf(qk, sizeof(qk), "\"%s\"", key);
  const char *v = strstr(json, qk);
  if (!v) return "";
  const char *colon = strchr(v, ':');
  const char *start = colon ? strchr(colon, '"') : nullptr;
  if (!start) return "";
  ++start;
  const char *end = strchr(start, '"');
  return end ? String(start, end - start) : String();
}
bool extractJsonDouble(const char *json, const char *key, double &out) {
  char qk[32];
  snprintf(qk, sizeof(qk), "\"%s\"", key);
  const char *v = strstr(json, qk);
  if (!v) return false;
  const char *colon = strchr(v, ':');
  if (!colon) return false;
  v = colon + 1;
  while (*v == ' ' || *v == '\t') ++v;
  char *end = nullptr;
  double val = strtod(v, &end);
  if (end == v) return false;
  out = val;
  return true;
}
bool extractJsonInt(const char *json, const char *key, int &out) {
  double d;
  if (!extractJsonDouble(json, key, d)) return false;
  out = (int)d;
  return true;
}
bool extractJsonBool(const char *json, const char *key) {
  char qk[32];
  snprintf(qk, sizeof(qk), "\"%s\"", key);
  const char *v = strstr(json, qk);
  if (!v) return false;
  const char *colon = strchr(v, ':');
  if (!colon) return false;
  v = colon + 1;
  while (*v == ' ' || *v == '\t') ++v;
  return strncmp(v, "true", 4) == 0;
}

uint8_t hidUsageForKey(const char *name) {
  if (!name) return 0;
  if (!strcmp(name, "Enter")) return 0x28;
  if (!strcmp(name, "Escape")) return 0x29;
  if (!strcmp(name, "Backspace")) return 0x2A;
  if (!strcmp(name, "Tab")) return 0x2B;
  if (!strcmp(name, "ArrowRight")) return 0x4F;
  if (!strcmp(name, "ArrowLeft")) return 0x50;
  if (!strcmp(name, "ArrowDown")) return 0x51;
  if (!strcmp(name, "ArrowUp")) return 0x52;
  if (strlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z')
    return (uint8_t)(0x04 + (name[0] - 'a'));
  if (strlen(name) == 1 && name[0] >= 'A' && name[0] <= 'Z')
    return (uint8_t)(0x04 + (name[0] - 'A'));
  return 0;
}

int8_t clamp8(int v) {
  if (v > 127) return 127;
  if (v < -128) return -128;
  return (int8_t)v;
}

// File serving
esp_err_t serveFile(httpd_req_t *req, const char *path, const char *ct) {
  File file;
  if (LittleFS.exists(path)) file = LittleFS.open(path, "r");
  else if (SPIFFS.exists(path)) file = SPIFFS.open(path, "r");
  if (!file) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    return ESP_OK;
  }
  httpd_resp_set_type(req, ct);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  uint8_t buf[512];
  while (true) {
    size_t n = file.read(buf, sizeof(buf));
    if (n == 0) break;
    if (httpd_resp_send_chunk(req, (const char *)buf, n) != ESP_OK) {
      file.close();
      return ESP_FAIL;
    }
  }
  file.close();
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// Static handlers
esp_err_t hRoot(httpd_req_t *req) {
  return serveFile(req, "/index.html", "text/html; charset=utf-8");
}
esp_err_t hJs(httpd_req_t *req) {
  return serveFile(req, "/app.js", "application/javascript; charset=utf-8");
}
esp_err_t hCss(httpd_req_t *req) {
  return serveFile(req, "/style.css", "text/css; charset=utf-8");
}
esp_err_t hManifest(httpd_req_t *req) {
  return serveFile(req, "/manifest.json", "application/manifest+json");
}

esp_err_t hWs(httpd_req_t *req) {
  auto *srv = static_cast<WebHidServer *>(req->user_ctx);
  int sock = httpd_req_to_sockfd(req);
  if (req->method == HTTP_GET) {
    return ESP_OK;
  }
  httpd_ws_frame_t frame = {};
  esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
  if (err != ESP_OK) return err;
  if (frame.len > 1024) return ESP_ERR_INVALID_SIZE;
  std::array<uint8_t, 1024> payload{};
  frame.payload = payload.data();
  if (frame.len > 0) {
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) return err;
  }
  if (frame.type == HTTPD_WS_TYPE_TEXT) {
    std::string msg((const char *)frame.payload, frame.len);
    // dispatch
    String type = extractJsonString(msg.c_str(), "type");
    if (type == "hello") {
      // acknowledge
      const char *pong = "{\"type\":\"pong\"}";
      httpd_ws_frame_t out = {};
      out.type = HTTPD_WS_TYPE_TEXT;
      out.payload = (uint8_t *)pong;
      out.len = strlen(pong);
      httpd_ws_send_frame(req, &out);
    } else if (type == "joystick") {
      double x = 0, y = 0;
      extractJsonDouble(msg.c_str(), "x", x);
      extractJsonDouble(msg.c_str(), "y", y);
      int dx = (int)(x * 20);
      int dy = (int)(y * 20);
      if (srv->_hid && srv->_hid->connected() && srv->_hid->reportsReady()) {
        srv->_hid->sendMouseReport(0, clamp8(dx), clamp8(dy), 0);
      }
    } else if (type == "button") {
      String id = extractJsonString(msg.c_str(), "id");
      uint8_t btns = 0;
      if (id == "left") btns = 0x01;
      else if (id == "right") btns = 0x02;
      if (srv->_hid && srv->_hid->connected() && srv->_hid->reportsReady()) {
        srv->_hid->sendMouseReport(btns, 0, 0, 0);
        delay(20);
        srv->_hid->sendMouseReport(0, 0, 0, 0);
      }
    } else if (type == "scroll") {
      int d = 0;
      extractJsonInt(msg.c_str(), "delta", d);
      if (srv->_hid && srv->_hid->connected() && srv->_hid->reportsReady()) {
        srv->_hid->sendMouseReport(0, 0, 0, (int8_t)(d > 0 ? 1 : -1));
      }
    } else if (type == "key") {
      String k = extractJsonString(msg.c_str(), "key");
      uint8_t u = hidUsageForKey(k.c_str());
      if (u && srv->_hid && srv->_hid->connected() && srv->_hid->reportsReady()) {
        srv->_hid->sendKey(u);
      }
    } else if (type == "hotkey") {
      String k = extractJsonString(msg.c_str(), "key");
      uint8_t u = hidUsageForKey(k.c_str());
      if (u && srv->_hid && srv->_hid->connected() && srv->_hid->reportsReady()) {
        srv->_hid->sendHotkey(0x01, u);
      }
    } else if (type == "gyro") {
      double gx = 0, gy = 0;
      extractJsonDouble(msg.c_str(), "gx", gx);
      extractJsonDouble(msg.c_str(), "gy", gy);
      int dx = (int)gx;
      int dy = (int)gy;
      if (srv->_hid && srv->_hid->connected() && srv->_hid->reportsReady()) {
        srv->_hid->sendMouseReport(0, clamp8(dx), clamp8(dy), 0);
      }
    } else if (type == "mic") {
      String action = extractJsonString(msg.c_str(), "action");
      if (!srv->_mic) {
        // ignore
      } else if (action == "live_start") {
        srv->_mic->externalLiveStart();
      } else if (action == "live_stop") {
        srv->_mic->externalLiveStop();
      } else if (action == "clip_start") {
        srv->_mic->externalClipStart();
      } else if (action == "clip_finish") {
        srv->_mic->externalClipFinish();
      } else if (action == "clip_end") {
        // ensure idle handled by update()
      }
    }
  } else if (frame.type == HTTPD_WS_TYPE_BINARY) {
    if (frame.len >= 640 && srv->_mic) {
      const int16_t *samples = reinterpret_cast<const int16_t *>(frame.payload);
      if (srv->_mic->status() == MicAudioStatus::Live) {
        srv->_mic->externalLiveFrame(samples);
      } else if (srv->_mic->status() == MicAudioStatus::Recording) {
        srv->_mic->externalClipFrame(samples);
      }
    }
  }
  return ESP_OK;
}

}  // namespace

bool WebHidServer::begin(BleHidDevice *hid, BleMicAudio *mic) {
  _hid = hid;
  _mic = mic;
  if (!LittleFS.begin(false)) {
    // try format
    LittleFS.begin(true);
  }
  if (!configureAllocator()) {
    // continue anyway
  }
  return true;
}

void WebHidServer::stopServer() {
  if (_httpsServer) {
    httpd_ssl_stop(_httpsServer);
    _httpsServer = nullptr;
  }
  _running = false;
}

bool WebHidServer::loadOrCreateCertificate() {
  // no-op placeholder; actual in setup
  return true;
}

bool WebHidServer::setupServer() {
  char base[32];
  snprintf(base, sizeof(base), "STICK-%04X",
           static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFF));
  char hostname[64];
  snprintf(hostname, sizeof(hostname), "%s-web.local", base);
  IPAddress localIp = WiFi.localIP();
  if (!_certPem.length() || !_keyPem.length()) {
    String c, k;
    if (!loadOrCreateCert(std::string(hostname), localIp, c, k)) {
      std::string cc, kk;
      int err = 0;
      if (!genCert(std::string(hostname), localIp, cc, kk, err)) return false;
      c = cc.c_str();
      k = kk.c_str();
    }
    _certPem = c;
    _keyPem = k;
  }

  httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
  config.httpd.max_open_sockets = 4;
  config.httpd.max_uri_handlers = 8;
  config.servercert = reinterpret_cast<const uint8_t *>(_certPem.c_str());
  config.servercert_len = _certPem.length() + 1;
  config.prvtkey_pem = reinterpret_cast<const uint8_t *>(_keyPem.c_str());
  config.prvtkey_len = _keyPem.length() + 1;
  config.port_secure = 443;

  esp_err_t err = httpd_ssl_start(&_httpsServer, &config);
  if (err != ESP_OK) {
    _httpsServer = nullptr;
    return false;
  }

  const auto reg = [this](const char *path, httpd_method_t m,
                          esp_err_t (*h)(httpd_req_t *), bool ws = false) {
    httpd_uri_t uri = {};
    uri.uri = path;
    uri.method = m;
    uri.handler = h;
    uri.user_ctx = this;
    uri.is_websocket = ws;
    return httpd_register_uri_handler(_httpsServer, &uri);
  };

  bool ok = reg("/", HTTP_GET, hRoot) == ESP_OK && reg("/app.js", HTTP_GET, hJs) == ESP_OK &&
            reg("/style.css", HTTP_GET, hCss) == ESP_OK &&
            reg("/manifest.json", HTTP_GET, hManifest) == ESP_OK &&
            reg("/ws", HTTP_GET, hWs, true) == ESP_OK;
  if (!ok) {
    stopServer();
    return false;
  }

  char mdnsHost[64];
  snprintf(mdnsHost, sizeof(mdnsHost), "%s-web", base);
  if (MDNS.begin(mdnsHost)) {
    MDNS.addService("https", "tcp", 443);
    Serial.printf("[webhid] mDNS available at https://%s.local/\n", mdnsHost);
  } else {
    Serial.println("[webhid] mDNS start failed");
  }
  return true;
}

namespace {
void webHidStartTask(void *arg) {
  auto *srv = static_cast<WebHidServer *>(arg);
  bool started = srv->setupServer();
  if (started) srv->_running = true;
  else srv->stopServer();
  srv->_serverStartInProgress = false;
  vTaskDelete(nullptr);
}
}  // namespace

void WebHidServer::loop(WifiOtaService *wifiOta) {
  (void)wifiOta;
  if (WiFi.status() != WL_CONNECTED) {
    if (_running || _httpsServer) stopServer();
    return;
  }
  if (!_running && !_serverStartInProgress) {
    uint32_t now = millis();
    if (_lastServerAttemptMs == 0 || now - _lastServerAttemptMs >= 5000) {
      _lastServerAttemptMs = now;
      _serverStartInProgress = true;
      const BaseType_t created = xTaskCreatePinnedToCore(
          webHidStartTask, "webhid_https_start", 16384, this, 3, nullptr, 1);
      if (created != pdPASS) {
        _serverStartInProgress = false;
        Serial.println("[webhid] ERROR: could not create HTTPS startup task");
      }
    }
  }
}

}  // namespace controller