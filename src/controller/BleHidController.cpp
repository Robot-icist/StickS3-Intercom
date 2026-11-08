#include "BleHidController.h"

#include <M5Unified.h>

#include <cstdio>

namespace controller {
namespace {

constexpr uint32_t kRenderIntervalMs = 100;

}  // namespace

bool BleHidController::begin() {
  if (!_ui.begin()) {
    return false;
  }
  if (!_features.begin()) {
    _ui.showError(_features.errorMessage());
    return false;
  }
  char hostname[32];
  snprintf(hostname, sizeof(hostname), "STICK-%04X",
           static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFF));
  _wifiOta.begin(hostname);
  if (!_hid.begin(_micAudio, [this](char* output, size_t outputSize) {
        return _wifiOta.createOtaSession(output, outputSize);
      })) {
    _ui.showError(_hid.errorMessage());
    return false;
  }
  return true;
}

void BleHidController::loop() {
  const uint32_t now = millis();
  M5.update();

  if (_hid.updateConnection(now)) {
    _features.onConnectionChanged();
  }
  _features.update(now);
  _micAudio.update(now);
  _wifiOta.loop();

  if (_wifiOta.otaInProgress()) {
    _features.resetIdleTimer();
  }

  if (now - _lastRenderAt >= kRenderIntervalMs) {
    _lastRenderAt = now;
    ControllerViewState state = _features.viewState(_hid.connected());
    state.hidReady = _hid.reportsReady();
    state.otaInProgress = _wifiOta.otaInProgress();
    state.otaProgress = _wifiOta.otaProgress();
    snprintf(state.otaStatus, sizeof(state.otaStatus), "%s",
             _wifiOta.otaStatus());
    snprintf(state.deviceName, sizeof(state.deviceName), "%s",
             _wifiOta.hostname());
    const String ipAddress = _wifiOta.ipAddress();
    snprintf(state.ipAddress, sizeof(state.ipAddress), "%s",
             ipAddress.c_str());
    snprintf(state.mdnsName, sizeof(state.mdnsName), "%s.local",
             _wifiOta.hostname());
    snprintf(state.otaPassword, sizeof(state.otaPassword), "%s",
             _wifiOta.otaPassword());
    _ui.render(state, now);
  }

  delay(2);
}

}  // namespace controller
