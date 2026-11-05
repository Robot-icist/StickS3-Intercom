#include "BleHidController.h"

#include <M5Unified.h>

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
  if (!_hid.begin(_micAudio)) {
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

  if (now - _lastRenderAt >= kRenderIntervalMs) {
    _lastRenderAt = now;
    ControllerViewState state = _features.viewState(_hid.connected());
    state.hidReady = _hid.reportsReady();
    _ui.render(state, now);
  }

  delay(2);
}

}  // namespace controller
