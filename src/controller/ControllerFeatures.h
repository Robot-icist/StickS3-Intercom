#pragma once

#include <Arduino.h>

#include "BleMicAudio.h"
#include "ControllerUi.h"

namespace controller {

class BleHidDevice;

class ControllerFeatures {
 public:
  enum class Mode : uint8_t {
    Mouse = 0,
    Navigation = 1,
    Orientation = 2,
    Dpi = 3,
    Microphone = 4
  };

  ControllerFeatures(BleHidDevice& hid, BleMicAudio& micAudio)
      : _hid(hid), _micAudio(micAudio) {}

  bool begin();
  void update(uint32_t now);
  void onConnectionChanged() { _haveMouseReport = false; }
  ControllerViewState viewState(bool connected) const;
  const char* errorMessage() const { return _errorMessage; }

 private:
  struct JoystickState {
    int16_t x = 0;
    int16_t y = 0;
    bool buttonPressed = false;
  };

  bool readJoystick(JoystickState& state);
  bool saveOrientationSettings();
  bool saveDpiLevel();
  bool saveMode(Mode mode);
  void applyMode(Mode mode);
  void toggleMicrophoneMode();
  void toggleJoystickFlip(bool flipX);
  void updateBattery(uint32_t now);
  void updateButtons(uint32_t now);
  void updateJoystick(uint32_t now);
  void updateImu(uint32_t now);
  void handleGesture(float gyroX, float gyroY, uint32_t now);
  void handleJoystickClick(bool doubleClick);

  BleHidDevice& _hid;
  BleMicAudio& _micAudio;
  const char* _errorMessage = "CONTROLLER INIT FAILED";
  Mode _mode = Mode::Mouse;
  Mode _micReturnMode = Mode::Mouse;
  bool _orientationSaved = true;
  bool _dpiSaved = true;
  bool _orientationStickCentered = true;
  bool _dpiStickCentered = true;
  bool _orientationInitialized = false;
  bool _previousButtonA = false;
  bool _previousButtonB = false;
  bool _buttonASuppressed = false;
  bool _buttonBSuppressed = false;
  bool _buttonALongHandled = false;
  bool _comboActive = false;
  bool _lastJoystickButton = false;
  bool _haveMouseReport = false;
  bool _havePendingJoystickClick = false;
  bool _joystickHoldHandled = false;
  bool _gestureArmed = true;
  bool _havePreviousImu = false;
  bool _joystickError = false;
  bool _flipX = false;
  bool _flipY = false;
  bool _pendingFlipX = false;
  bool _pendingFlipY = false;
  bool _modePickerActive = false;
  bool _modePickerStickCentered = false;
  bool _modePickerIgnoreButtonRelease = false;
  uint8_t _joystickErrorCount = 0;
  uint8_t _selectedMode = 0;
  uint8_t _dpiLevel = 3;
  int8_t _batteryLevel = -1;
  uint8_t _baseRotation = 0;
  uint8_t _pendingBaseRotation = 0;
  float _roll = 0.0f;
  float _pitch = 0.0f;
  float _yaw = 0.0f;

  uint32_t _buttonAStartedAt = 0;
  uint32_t _buttonBStartedAt = 0;
  uint32_t _joystickPressedAt = 0;
  uint32_t _lastJoystickClickAt = 0;
  uint32_t _lastJoystickAt = 0;
  uint32_t _lastDpiAdjustAt = 0;
  uint32_t _lastImuAt = 0;
  uint32_t _lastBatteryAt = 0;
  uint32_t _lastJoystickLogAt = 0;
  uint32_t _lastGestureAt = 0;
  uint32_t _lastValidInputAt = 0;
  uint32_t _lastGestureKeyAt = 0;
};

}  // namespace controller
