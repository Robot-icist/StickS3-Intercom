#include "ControllerFeatures.h"

#include <M5Unified.h>
#include <Preferences.h>

#include <algorithm>
#include <cmath>

#include "BleHidDevice.h"

namespace controller
{
  namespace
  {

    constexpr uint8_t kJoystickAddress = 0x63;
    constexpr uint8_t kJoystickOffsetRegister = 0x50;
    constexpr uint8_t kJoystickButtonRegister = 0x20;
    constexpr uint32_t kI2cFrequency = 400000;
    constexpr uint8_t kJoystickProbeAttempts = 12;
    constexpr uint32_t kJoystickProbeDelayMs = 100;
    constexpr uint32_t kInputIntervalMs = 20;
    constexpr uint32_t kBatteryIntervalMs = 5000;
    constexpr uint32_t kModeHoldMs = 900;
    constexpr uint32_t kClickMaxMs = 600;
    constexpr uint32_t kButtonHoldMs = 700;
    constexpr uint32_t kDoubleClickWindowMs = 350;
    constexpr uint32_t kGestureCooldownMs = 800;
    constexpr float kGestureRearmDps = 100.0f;
    constexpr int32_t kJoystickOrientationThreshold = 900;
    constexpr int32_t kJoystickCenterThreshold = 350;
    constexpr float kGesturePitchDps = 240.0f;
    constexpr float kGestureTwistDps = 300.0f;
    constexpr float kDegToRad = 0.017453293f;
    constexpr uint8_t kKeyC = 0x06;
    constexpr uint8_t kKeyV = 0x19;
    constexpr uint8_t kKeyEnter = 0x28;
    constexpr uint8_t kKeyEscape = 0x29;
    constexpr uint8_t kKeyPageUp = 0x4B;
    constexpr uint8_t kKeyArrowRight = 0x4F;
    constexpr uint8_t kKeyArrowLeft = 0x50;
    constexpr uint8_t kKeyPageDown = 0x4E;

    int8_t axisToMouse(int16_t value, uint8_t dpiLevel)
    {
      constexpr int32_t deadzone = 180;
      constexpr int32_t maxValue = 2047;
      constexpr int32_t maxSteps[] = {5, 8, 11, 14, 18, 24, 32};
      const int32_t maxStep = maxSteps[std::min<uint8_t>(dpiLevel, 6)];
      int32_t magnitude = value;
      if (magnitude < 0)
      {
        magnitude = -magnitude;
      }
      if (magnitude <= deadzone)
      {
        return 0;
      }

      const int32_t normalized = std::min<int32_t>(
          127, (magnitude - deadzone) * 127 / (maxValue - deadzone));
      const int32_t step =
          std::max<int32_t>(1, (normalized * normalized * maxStep) / (127 * 127));
      return static_cast<int8_t>(value < 0 ? -step : step);
    }

    template <typename T>
    void rotateAxes(T &x, T &y, uint8_t quarterTurns)
    {
      const T oldX = x;
      const T oldY = y;
      switch (quarterTurns & 0x03)
      {
      case 1:
        x = -oldY;
        y = oldX;
        break;
      case 2:
        x = -oldX;
        y = -oldY;
        break;
      case 3:
        x = oldY;
        y = -oldX;
        break;
      default:
        break;
      }
    }

  } // namespace

  bool ControllerFeatures::begin()
  {
    if (!M5.Imu.isEnabled())
    {
      Serial.println("[controller] StickS3 IMU is unavailable");
      _errorMessage = "IMU NOT FOUND";
      return false;
    }

    bool joystickFound = false;
    for (uint8_t attempt = 0; attempt < kJoystickProbeAttempts; ++attempt)
    {
      if (M5.Ex_I2C.scanID(kJoystickAddress, kI2cFrequency))
      {
        joystickFound = true;
        break;
      }
      delay(kJoystickProbeDelayMs);
    }
    if (!joystickFound)
    {
      Serial.println(
          "[controller] Joystick2 not detected yet; continuing and retrying");
      _joystickError = true;
    }

    Preferences preferences;
    // Writable open also creates the namespace on a fresh device.
    if (!preferences.begin("blehid", false))
    {
      Serial.println("[controller] failed to open NVS preferences");
      _errorMessage = "NVS INIT FAILED";
      return false;
    }
    const uint8_t savedRotation = preferences.getUChar("baseRot", 0);
    const uint8_t savedMode = preferences.getUChar("mode", 0);
    const uint8_t savedFlipX = preferences.getUChar("flipX", 0);
    const uint8_t savedFlipY = preferences.getUChar("flipY", 0);
    const uint8_t savedDpi = preferences.getUChar("dpi", 3);
    preferences.end();
    if (savedRotation > 3 || savedMode > 4 || savedFlipX > 1 ||
        savedFlipY > 1 || savedDpi > 6)
    {
      Serial.printf("[controller] invalid saved settings: rotation=%u mode=%u flipX=%u flipY=%u dpi=%u\n",
                    static_cast<unsigned>(savedRotation),
                    static_cast<unsigned>(savedMode),
                    static_cast<unsigned>(savedFlipX),
                    static_cast<unsigned>(savedFlipY),
                    static_cast<unsigned>(savedDpi));
      _errorMessage = "INVALID SETTINGS";
      return false;
    }

    _baseRotation = savedRotation;
    _pendingBaseRotation = _baseRotation;
    _flipX = savedFlipX != 0;
    _flipY = savedFlipY != 0;
    _pendingFlipX = _flipX;
    _pendingFlipY = _flipY;
    _dpiLevel = savedDpi;
    _mode = static_cast<Mode>(savedMode);
    _lastValidInputAt = millis();
    return true;
  }

  void ControllerFeatures::update(uint32_t now)
  {
    updateBattery(now);
    updateButtons(now);

    if (now - _lastJoystickAt >= kInputIntervalMs)
    {
      _lastJoystickAt = now;
      updateJoystick(now);
    }

    if (now - _lastImuAt >= kInputIntervalMs)
    {
      updateImu(now);
      _lastImuAt = now;
    }
  }

  void ControllerFeatures::updateBattery(uint32_t now)
  {
    if (now - _lastBatteryAt < kBatteryIntervalMs)
    {
      return;
    }
    _lastBatteryAt = now;

    // M5Unified knows the StickS3's M5PM1 PMIC and reports 0..100, or a negative
    // value when the battery is not readable (for example on USB with no cell).
    const int32_t level = M5.Power.getBatteryLevel();
    _batteryLevel =
        (level >= 0 && level <= 100) ? static_cast<int8_t>(level) : -1;
  }

  ControllerViewState ControllerFeatures::viewState(bool connected) const
  {
    ControllerViewState state;
    state.navigationMode = _mode == Mode::Navigation;
    state.dpiMode = _mode == Mode::Dpi;
    state.micMode = _mode == Mode::Microphone;
    state.modePickerActive = _modePickerActive;
    state.selectedMode = _selectedMode;
    state.micSubscribed = _micAudio.subscribed();
    state.micStatus = _micAudio.status();
    state.connected = connected;
    state.demoMode = _mode == Mode::Orientation;
    state.orientationSaved = _orientationSaved;
    state.dpiSaved = _dpiSaved;
    state.flipX = _mode == Mode::Orientation ? _pendingFlipX : _flipX;
    state.flipY = _mode == Mode::Orientation ? _pendingFlipY : _flipY;
    state.dpiLevel = _dpiLevel;
    state.batteryLevel = _batteryLevel;
    state.joystickError = _joystickError;
    state.pendingBaseRotation = _pendingBaseRotation;
    state.roll = _roll;
    state.pitch = _pitch;
    state.yaw = _yaw;
    state.lastValidInputAt = _lastValidInputAt;
    return state;
  }

  bool ControllerFeatures::readJoystick(JoystickState &state)
  {
    uint8_t axes[4] = {};
    uint8_t button = 1;
    if (!M5.Ex_I2C.readRegister(kJoystickAddress, kJoystickOffsetRegister, axes,
                                sizeof(axes), kI2cFrequency) ||
        !M5.Ex_I2C.readRegister(kJoystickAddress, kJoystickButtonRegister,
                                &button, sizeof(button), kI2cFrequency))
    {
      return false;
    }

    state.x = static_cast<int16_t>(
        static_cast<uint16_t>(axes[0]) |
        (static_cast<uint16_t>(axes[1]) << 8));
    state.y = static_cast<int16_t>(
        static_cast<uint16_t>(axes[2]) |
        (static_cast<uint16_t>(axes[3]) << 8));
    state.buttonPressed = button == 0;
    return true;
  }

  bool ControllerFeatures::saveOrientationSettings()
  {
    Preferences preferences;
    if (!preferences.begin("blehid", false))
    {
      Serial.println("[controller] failed to open NVS to save base orientation");
      return false;
    }
    const uint8_t rotation = _pendingBaseRotation & 0x03;
    const size_t rotationWritten = preferences.putUChar("baseRot", rotation);
    const size_t flipXWritten = preferences.putUChar("flipX", _pendingFlipX);
    const size_t flipYWritten = preferences.putUChar("flipY", _pendingFlipY);
    preferences.end();
    if (rotationWritten != sizeof(uint8_t) ||
        flipXWritten != sizeof(uint8_t) || flipYWritten != sizeof(uint8_t))
    {
      Serial.println("[controller] failed to persist joystick orientation");
      return false;
    }

    _baseRotation = rotation;
    _pendingBaseRotation = _baseRotation;
    _flipX = _pendingFlipX;
    _flipY = _pendingFlipY;
    Serial.printf("[controller] saved joystick orientation: %u degrees, flipX=%u flipY=%u\n",
                  static_cast<unsigned>(_baseRotation * 90),
                  static_cast<unsigned>(_flipX), static_cast<unsigned>(_flipY));
    return true;
  }

  bool ControllerFeatures::saveDpiLevel()
  {
    Preferences preferences;
    if (!preferences.begin("blehid", false))
    {
      Serial.println("[controller] failed to open NVS to save mouse speed");
      return false;
    }
    const size_t written = preferences.putUChar("dpi", _dpiLevel);
    preferences.end();
    if (written != sizeof(uint8_t))
    {
      Serial.println("[controller] failed to persist mouse speed");
      return false;
    }
    return true;
  }

  bool ControllerFeatures::saveMode(Mode mode)
  {
    Preferences preferences;
    if (!preferences.begin("blehid", false))
    {
      Serial.println("[controller] failed to open NVS to save controller mode");
      return false;
    }
    const size_t written = preferences.putUChar("mode", static_cast<uint8_t>(mode));
    preferences.end();
    if (written != sizeof(uint8_t))
    {
      Serial.println("[controller] failed to persist controller mode");
      return false;
    }
    return true;
  }

  void ControllerFeatures::applyMode(Mode mode)
  {
    if (mode == _mode)
    {
      return;
    }
    if (_mode == Mode::Microphone)
    {
      _micAudio.cancel();
    }
    else
    {
      // Remember where to return when the next quick switch leaves the mic.
      _micReturnMode = _mode;
    }

    _mode = mode;
    _havePendingJoystickClick = false;
    if (_mode == Mode::Orientation)
    {
      _pendingBaseRotation = _baseRotation;
      _pendingFlipX = _flipX;
      _pendingFlipY = _flipY;
      _orientationSaved = true;
      _orientationStickCentered = true;
    }
    else if (!_orientationSaved)
    {
      _pendingBaseRotation = _baseRotation;
      _pendingFlipX = _flipX;
      _pendingFlipY = _flipY;
    }
    if (_mode == Mode::Dpi)
    {
      _dpiSaved = true;
      _dpiStickCentered = true;
    }
    _orientationInitialized = false;
    _yaw = 0.0f;
    if (!saveMode(_mode))
    {
      Serial.println("[controller] mode changed for this session only");
    }
    static const char *const modeNames[] = {
        "mouse", "navigation", "orientation", "dpi", "microphone"};
    Serial.printf("[controller] mode: %s\n",
                  modeNames[static_cast<uint8_t>(_mode)]);
  }

  void ControllerFeatures::toggleMicrophoneMode()
  {
    const bool enterMic = _mode != Mode::Microphone;
    applyMode(enterMic ? Mode::Microphone : _micReturnMode);
    Serial.printf("[controller] quick switch: %s\n",
                  enterMic ? "microphone" : "previous mode");
  }

  void ControllerFeatures::toggleJoystickFlip(bool flipX)
  {
    bool &flip = flipX ? _pendingFlipX : _pendingFlipY;
    flip = !flip;
    _orientationSaved = false;
    Serial.printf("[controller] pending joystick flip: X=%u Y=%u\n",
                  static_cast<unsigned>(_pendingFlipX),
                  static_cast<unsigned>(_pendingFlipY));
  }

  void ControllerFeatures::updateButtons(uint32_t now)
  {
    const bool buttonA = M5.BtnA.isPressed();
    const bool buttonB = M5.BtnB.isPressed();

    if (buttonA && !_previousButtonA)
    {
      _buttonAStartedAt = now;
      _buttonASuppressed = false;
      _buttonALongHandled = false;
    }
    if (buttonB && !_previousButtonB)
    {
      _buttonBStartedAt = now;
      _buttonBSuppressed = false;
    }

    if (buttonA && buttonB)
    {
      if (!_comboActive)
      {
        _comboActive = true;
        _buttonASuppressed = true;
        _buttonBSuppressed = true;
      }
    }

    if (_modePickerActive)
    {
      if (_previousButtonB && !buttonB &&
          now - _buttonBStartedAt <= kClickMaxMs)
      {
        _modePickerActive = false;
        Serial.println("[controller] mode selection cancelled");
      }
      _buttonASuppressed = buttonA;
      _buttonBSuppressed = buttonB;
      _previousButtonA = buttonA;
      _previousButtonB = buttonB;
      return;
    }

    // Hold BtnA to jump between Microphone and the previous mode. Don't leave
    // the mic while a clip is recording or being sent.
    if (buttonA && !_buttonALongHandled && !_comboActive &&
        now - _buttonAStartedAt >= kButtonHoldMs)
    {
      _buttonALongHandled = true;
      _buttonASuppressed = true;
      const MicAudioStatus micStatus = _micAudio.status();
      const bool micBusy =
          _mode == Mode::Microphone &&
          (micStatus == MicAudioStatus::Recording ||
           micStatus == MicAudioStatus::Sending);
      if (!micBusy)
      {
        toggleMicrophoneMode();
      }
    }

    if (_previousButtonA && !buttonA && !_buttonASuppressed &&
        now - _buttonAStartedAt <= kClickMaxMs)
    {
      if (_mode == Mode::Orientation)
      {
        toggleJoystickFlip(true);
      }
      else if (_mode == Mode::Microphone)
      {
        _micAudio.toggleClipRecording();
      }
      else if (_mode == Mode::Navigation)
      {
        _hid.sendKey(kKeyPageUp);
      }
      else
      {
        _hid.sendHotkey(kKeyC);
      }
      _lastGestureKeyAt = now;
    }
    if (_previousButtonB && !buttonB && !_buttonBSuppressed &&
        now - _buttonBStartedAt <= kClickMaxMs)
    {
      if (_mode == Mode::Orientation)
      {
        toggleJoystickFlip(false);
      }
      else if (_mode == Mode::Microphone)
      {
        _micAudio.cancel();
      }
      else if (_mode == Mode::Navigation)
      {
        _hid.sendKey(kKeyPageDown);
      }
      else
      {
        _hid.sendHotkey(kKeyV);
      }
      _lastGestureKeyAt = now;
    }

    if (_comboActive && !buttonA && !buttonB)
    {
      _comboActive = false;
    }

    if (!buttonA && !_comboActive)
    {
      _buttonASuppressed = false;
    }
    if (!buttonB && !_comboActive)
    {
      _buttonBSuppressed = false;
    }

    _previousButtonA = buttonA;
    _previousButtonB = buttonB;
  }

  void ControllerFeatures::updateJoystick(uint32_t now)
  {
    JoystickState joystick;
    if (!readJoystick(joystick))
    {
      if (_joystickErrorCount < UINT8_MAX)
      {
        ++_joystickErrorCount;
      }
      _joystickError = true;
      if (_lastJoystickButton)
      {
        if (_mode == Mode::Microphone &&
            _micAudio.status() == MicAudioStatus::Live)
        {
          _micAudio.toggleLive();
        }
        _hid.sendMouseReport(0, 0, 0);
        _lastJoystickButton = false;
        _modePickerIgnoreButtonRelease = false;
        _haveMouseReport = true;
      }
      if (_joystickErrorCount == 1)
      {
        Serial.println("[controller] Joystick2 I2C read failed");
      }
      return;
    }

    _joystickError = false;
    _joystickErrorCount = 0;
    _lastValidInputAt = now;
    if (now - _lastJoystickLogAt >= 1000)
    {
      _lastJoystickLogAt = now;
      static const char *const modeNames[] = {
          "mouse", "navigation", "orientation", "dpi", "microphone"};
      Serial.printf("[joystick] x=%d y=%d button=%s mode=%s\n", joystick.x,
                    joystick.y, joystick.buttonPressed ? "pressed" : "released",
                    modeNames[static_cast<uint8_t>(_mode)]);
    }
    if (_havePendingJoystickClick &&
        now - _lastJoystickClickAt > kDoubleClickWindowMs)
    {
      _havePendingJoystickClick = false;
      handleJoystickClick(false);
    }

    int32_t x = joystick.x;
    int32_t y = joystick.y;
    if (joystick.buttonPressed && !_lastJoystickButton)
    {
      _joystickPressedAt = now;
      _joystickHoldHandled = false;
      if (_mode == Mode::Microphone &&
          _micAudio.status() == MicAudioStatus::Idle)
      {
        _micAudio.toggleLive();
      }
    }
    if (joystick.buttonPressed && !_joystickHoldHandled &&
        _mode != Mode::Microphone &&
        now - _joystickPressedAt >= kModeHoldMs)
    {
      _modePickerActive = true;
      _selectedMode = static_cast<uint8_t>(_mode);
      _modePickerStickCentered =
          std::abs(x) < kJoystickCenterThreshold &&
          std::abs(y) < kJoystickCenterThreshold;
      _modePickerIgnoreButtonRelease = true;
      _havePendingJoystickClick = false;
      _joystickHoldHandled = true;
      Serial.println("[controller] mode selection opened");
    }

    if (_modePickerActive)
    {
      if (std::abs(y) < kJoystickCenterThreshold)
      {
        _modePickerStickCentered = true;
      }
      else if (_modePickerStickCentered &&
               std::abs(y) >= kJoystickOrientationThreshold)
      {
        _selectedMode = static_cast<uint8_t>(
            (_selectedMode + (y > 0 ? 1 : 4)) % 5);
        _modePickerStickCentered = false;
      }

      if (!joystick.buttonPressed && _lastJoystickButton)
      {
        if (_modePickerIgnoreButtonRelease)
        {
          _modePickerIgnoreButtonRelease = false;
        }
        else if (now - _joystickPressedAt <= kClickMaxMs)
        {
          const Mode selected = static_cast<Mode>(_selectedMode);
          _modePickerActive = false;
          applyMode(selected);
        }
      }
      _lastJoystickButton = joystick.buttonPressed;
      _haveMouseReport = false;
      return;
    }

    if (_mode == Mode::Orientation)
    {
      if (std::abs(x) < kJoystickCenterThreshold)
      {
        _orientationStickCentered = true;
      }
      else if (_orientationStickCentered &&
               std::abs(x) >= kJoystickOrientationThreshold)
      {
        _pendingBaseRotation =
            (_pendingBaseRotation + (x > 0 ? 1 : 3)) & 0x03;
        _orientationSaved = false;
        _orientationStickCentered = false;
        _orientationInitialized = false;
        _yaw = 0.0f;
        Serial.printf("[controller] pending base orientation: %u degrees\n",
                      static_cast<unsigned>(_pendingBaseRotation * 90));
      }

      if (!joystick.buttonPressed && _lastJoystickButton &&
          !_joystickHoldHandled &&
          now - _joystickPressedAt <= kClickMaxMs)
      {
        _orientationSaved = saveOrientationSettings();
      }
      _lastJoystickButton = joystick.buttonPressed;
      _haveMouseReport = false;
      return;
    }

    rotateAxes(x, y, _baseRotation);
    if (_flipX)
    {
      x = -x;
    }
    if (_flipY)
    {
      y = -y;
    }

    if (_mode == Mode::Dpi)
    {
      if (std::abs(x) < kJoystickCenterThreshold)
      {
        _dpiStickCentered = true;
      }
      else if (_dpiStickCentered &&
               std::abs(x) >= kJoystickOrientationThreshold &&
               now - _lastDpiAdjustAt >= 250)
      {
        if (x > 0 && _dpiLevel < 6)
        {
          ++_dpiLevel;
        }
        else if (x < 0 && _dpiLevel > 0)
        {
          --_dpiLevel;
        }
        _dpiStickCentered = false;
        _lastDpiAdjustAt = now;
        _dpiSaved = saveDpiLevel();
        Serial.printf("[controller] mouse speed level %u (%s)\n",
                      static_cast<unsigned>(_dpiLevel),
                      _dpiSaved ? "saved" : "not saved");
      }

      if (!joystick.buttonPressed && _lastJoystickButton &&
          !_joystickHoldHandled && now - _joystickPressedAt <= kClickMaxMs)
      {
        _dpiSaved = saveDpiLevel();
      }
      _lastJoystickButton = joystick.buttonPressed;
      _haveMouseReport = false;
      return;
    }

    if (!joystick.buttonPressed && _lastJoystickButton &&
        _mode == Mode::Microphone &&
        _micAudio.status() == MicAudioStatus::Live)
    {
      _micAudio.toggleLive();
    }
    if (!joystick.buttonPressed && _lastJoystickButton &&
        !_joystickHoldHandled &&
        now - _joystickPressedAt <= kClickMaxMs)
    {
      if (_havePendingJoystickClick &&
          now - _lastJoystickClickAt <= kDoubleClickWindowMs)
      {
        _havePendingJoystickClick = false;
        handleJoystickClick(true);
      }
      else
      {
        _havePendingJoystickClick = true;
        _lastJoystickClickAt = now;
      }
    }
    _lastJoystickButton = joystick.buttonPressed;

    if (_mode == Mode::Navigation)
    {
      const int8_t wheel = axisToMouse(static_cast<int16_t>(y), _dpiLevel);
      if (wheel != 0)
      {
        _hid.sendMouseReport(0, 0, 0, static_cast<int8_t>(-wheel));
      }
      _haveMouseReport = true;
      return;
    }

    const int8_t dx = axisToMouse(static_cast<int16_t>(x), _dpiLevel);
    const int8_t dy = axisToMouse(static_cast<int16_t>(y), _dpiLevel);
    if (dx != 0 || dy != 0 || !_haveMouseReport)
    {
      _hid.sendMouseReport(0, dx, static_cast<int8_t>(-dy));
      _haveMouseReport = true;
    }
  }

  void ControllerFeatures::updateImu(uint32_t now)
  {
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;
    if (!M5.Imu.getAccel(&ax, &ay, &az) || !M5.Imu.getGyro(&gx, &gy, &gz))
    {
      return;
    }

    const uint8_t rotation =
        _mode == Mode::Orientation ? _pendingBaseRotation : _baseRotation;
    float accelX = ax;
    float accelY = ay;
    float gyroX = gx;
    float gyroY = gy;
    rotateAxes(accelX, accelY, rotation);
    rotateAxes(gyroX, gyroY, rotation);

    float dt = 0.02f;
    if (_havePreviousImu)
    {
      dt = std::min(0.05f, (now - _lastImuAt) / 1000.0f);
    }
    _havePreviousImu = true;

    const float accelRoll = atan2f(accelY, az);
    const float accelPitch =
        atan2f(-accelX, sqrtf(accelY * accelY + az * az));
    if (!_orientationInitialized)
    {
      _roll = accelRoll;
      _pitch = accelPitch;
      _orientationInitialized = true;
    }
    else
    {
      constexpr float gyroWeight = 0.96f;
      _roll = gyroWeight * (_roll + gyroX * kDegToRad * dt) +
              (1.0f - gyroWeight) * accelRoll;
      _pitch = gyroWeight * (_pitch + gyroY * kDegToRad * dt) +
               (1.0f - gyroWeight) * accelPitch;
    }
    _yaw += gz * kDegToRad * dt;
    if (_yaw > 2.0f * PI)
    {
      _yaw -= 2.0f * PI;
    }
    else if (_yaw < -2.0f * PI)
    {
      _yaw += 2.0f * PI;
    }

    if (_mode != Mode::Orientation)
    {
      handleGesture(gyroX, gyroY, now);
    }
  }

  void ControllerFeatures::handleGesture(float gyroX, float gyroY,
                                         uint32_t now)
  {
    if (now - _lastGestureAt < kGestureCooldownMs ||
        now - _lastGestureKeyAt < kGestureCooldownMs)
    {
      return;
    }

    if (!_gestureArmed)
    {
      if (fabsf(gyroX) < kGestureRearmDps &&
          fabsf(gyroY) < kGestureRearmDps)
      {
        _gestureArmed = true;
      }
      return;
    }

    if (fabsf(gyroX) >= kGesturePitchDps)
    {
      const uint8_t key = gyroX > 0.0f ? kKeyEnter : kKeyEscape;
      _hid.sendKey(key);
      Serial.printf("[gesture] front/back -> %s\n",
                    key == kKeyEnter ? "accept" : "cancel");
    }
    else if (fabsf(gyroY) >= kGestureTwistDps)
    {
      const uint8_t key = gyroY > 0.0f ? kKeyArrowRight : kKeyArrowLeft;
      _hid.sendKey(key);
      Serial.printf("[gesture] left/right -> %s option\n",
                    key == kKeyArrowRight ? "next" : "previous");
    }
    else
    {
      return;
    }
    _gestureArmed = false;
    _lastGestureAt = now;
    _lastGestureKeyAt = now;
  }

  void ControllerFeatures::handleJoystickClick(bool doubleClick)
  {
    if (_mode == Mode::Navigation)
    {
      const uint8_t key = doubleClick ? kKeyEscape : kKeyEnter;
      _hid.sendKey(key);
      Serial.printf("[controller] navigation %s -> %s\n",
                    doubleClick ? "double-click" : "click",
                    doubleClick ? "Escape" : "Enter");
      return;
    }

    if (doubleClick)
    {
      _hid.sendMouseReport(0x02, 0, 0);
      delay(8);
      _hid.sendMouseReport(0, 0, 0);
      Serial.println("[controller] joystick double-click -> right click");
    }
    else
    {
      _hid.sendMouseReport(0x01, 0, 0);
      delay(8);
      _hid.sendMouseReport(0, 0, 0);
      Serial.println("[controller] joystick click -> left click");
    }
  }

} // namespace controller
