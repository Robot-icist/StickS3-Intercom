#include "ControllerFeatures.h"
#include "ControllerConstants.h"

#include <M5Unified.h>
#include <Preferences.h>
#include <esp_sleep.h>

#include <algorithm>
#include <cmath>

#include "BleHidDevice.h"

namespace controller
{


  constexpr ControllerFeatures::Mode kModeOrder[] = {
      ControllerFeatures::Mode::Mouse,
      ControllerFeatures::Mode::AirMouse,
      ControllerFeatures::Mode::Navigation,
      ControllerFeatures::Mode::Magnifier,
      ControllerFeatures::Mode::Microphone,
      ControllerFeatures::Mode::Dpi,
      ControllerFeatures::Mode::Orientation,
      ControllerFeatures::Mode::Style,
      ControllerFeatures::Mode::Brightness,
      ControllerFeatures::Mode::Sleep,
  };
  constexpr uint8_t kPickerModeCount =
      sizeof(kModeOrder) / sizeof(kModeOrder[0]);

  uint8_t ControllerFeatures::modeOrderIndex(Mode mode)
  {
    for (uint8_t index = 0; index < kPickerModeCount; ++index)
    {
      if (kModeOrder[index] == mode)
      {
        return index;
      }
    }
    return 0;
  }

  ControllerFeatures::Mode ControllerFeatures::modeAtOrderIndex(uint8_t index)
  {
    return kModeOrder[index % kPickerModeCount];
  }

  const char *ControllerFeatures::modeName(Mode mode)
  {
    switch (mode)
    {
    case Mode::Mouse:
      return "mouse";
    case Mode::Navigation:
      return "navigation";
    case Mode::Orientation:
      return "orientation";
    case Mode::Dpi:
      return "dpi";
    case Mode::Microphone:
      return "microphone";
    case Mode::AirMouse:
      return "air mouse";
    case Mode::Magnifier:
      return "magnifier";
    case Mode::Style:
      return "style";
    case Mode::Brightness:
      return "brightness";
    case Mode::Sleep:
      return "sleep";
    }
    return "unknown";
  }

  const char *ControllerFeatures::paletteName(ColorPalette palette)
  {
    switch (palette)
    {
    case ColorPalette::Default:
      return "Default";
    case ColorPalette::Dark:
      return "Dark";
    case ColorPalette::Light:
      return "Light";
    case ColorPalette::HighContrast:
      return "High Contrast";
    case ColorPalette::Cyberpunk:
      return "Cyberpunk";
    case ColorPalette::Monochrome:
      return "Monochrome";
    case ColorPalette::Sepia:
      return "Sepia";
    case ColorPalette::Forest:
      return "Forest";
    }
    return "Unknown";
  }

  bool ControllerFeatures::begin()
  {
    _lastBatteryAt = millis();
    readBatteryLevel();

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
    const uint8_t savedMotion = preferences.getUChar("motion", 3);
    const uint8_t savedMotionFlipX = preferences.getUChar("mflipX", 0);
    const uint8_t savedMotionFlipY = preferences.getUChar("mflipY", 0);
    const uint32_t savedSleepMs = preferences.getUInt("sleepMs", kDefaultIdleSleepMs);
    const uint8_t savedBrightness = preferences.getUChar("brightness", 180);
    preferences.end();
    if (savedRotation > 3 || savedMode >= kModeCount || savedFlipX > 1 ||
        savedFlipY > 1 || savedDpi > 6 || savedMotion > 6 ||
        savedMotionFlipX > 1 || savedMotionFlipY > 1)
    {
      Serial.printf("[controller] invalid saved settings: rotation=%u mode=%u flipX=%u flipY=%u dpi=%u motion=%u mflipX=%u mflipY=%u\n",
                    static_cast<unsigned>(savedRotation),
                    static_cast<unsigned>(savedMode),
                    static_cast<unsigned>(savedFlipX),
                    static_cast<unsigned>(savedFlipY),
                    static_cast<unsigned>(savedDpi),
                    static_cast<unsigned>(savedMotion),
                    static_cast<unsigned>(savedMotionFlipX),
                    static_cast<unsigned>(savedMotionFlipY));
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
    _motionLevel = savedMotion;
    _motionFlipX = savedMotionFlipX != 0;
    _motionFlipY = savedMotionFlipY != 0;
    _mode = static_cast<Mode>(savedMode);
    _colorPalette = loadColorPalette();
    _selectedPaletteIndex = static_cast<uint8_t>(_colorPalette);
    _styleSaved = true;
    _sleepTimeoutMs = savedSleepMs;
    _brightness = savedBrightness;
    _selectedSleepIndex = 1;
    M5.Display.setBrightness(_brightness);
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

    if (_modePickerActive)
    {
      _lastValidInputAt = now;
    }

    if (_sleepTimeoutMs > 0 &&
        _micAudio.status() != MicAudioStatus::Recording &&
        _micAudio.status() != MicAudioStatus::Sending &&
        _micAudio.status() != MicAudioStatus::Live)
    {
      if (now - _lastValidInputAt >= _sleepTimeoutMs)
      {
        enterDeepSleep();
      }
    }
    else
    {
      _lastValidInputAt = now;
    }
  }

  void ControllerFeatures::updateBattery(uint32_t now)
  {
    if (now - _lastBatteryAt < kBatteryIntervalMs)
    {
      return;
    }
    _lastBatteryAt = now;
    readBatteryLevel();
  }

  void ControllerFeatures::readBatteryLevel()
  {
    // M5Unified knows the StickS3's M5PM1 PMIC and reports 0..100, or a negative
    // value when the battery is not readable (for example on USB with no cell).
    const int32_t level = M5.Power.getBatteryLevel();
    if (level < 0 || level > 100)
    {
      _batteryLevel = -1;
      return;
    }

    const auto batteryLevel = static_cast<int8_t>(level);
    if (batteryLevel != _batteryLevel)
    {
      Serial.printf("[controller] battery %d%%\n",
                    static_cast<int>(batteryLevel));
    }
    _batteryLevel = batteryLevel;
    // Publish through the HID Battery Service so Windows displays the real
    // pack level instead of a fixed value.
    _hid.setBatteryLevel(static_cast<uint8_t>(batteryLevel));
  }

  ControllerViewState ControllerFeatures::viewState(bool connected) const
  {
    ControllerViewState state;
    state.navigationMode = _mode == Mode::Navigation;
    state.dpiMode = _mode == Mode::Dpi;
    state.micMode = _mode == Mode::Microphone;
    state.airMouseMode = _mode == Mode::AirMouse;
    state.magnifierMode = _mode == Mode::Magnifier;
    state.magnifierOpen = _magnifierOpen;
    state.modePickerActive = _modePickerActive;
    state.selectedMode = _selectedMode;
    state.micSubscribed = _micAudio.subscribed();
    state.micStatus = _micAudio.status();
    state.connected = connected;
    state.demoMode = _mode == Mode::Orientation;
    state.styleMode = _mode == Mode::Style;
    state.brightnessMode = _mode == Mode::Brightness;
    state.brightnessSaved = _brightnessSaved;
    state.brightness = _brightness;
    state.sleepMode = _mode == Mode::Sleep;
    state.sleepTimeoutMs = _sleepTimeoutMs;
    state.orientationSaved = _orientationSaved;
    state.dpiSaved = _dpiSaved;
    state.motionSaved = _motionSaved;
    state.motionFlipSaved = _motionFlipSaved;
    state.styleSaved = _styleSaved;
    state.flipX = _mode == Mode::Orientation ? _pendingFlipX : _flipX;
    state.flipY = _mode == Mode::Orientation ? _pendingFlipY : _flipY;
    state.motionFlipX = _motionFlipX;
    state.motionFlipY = _motionFlipY;
    state.dpiLevel = _dpiLevel;
    state.motionLevel = _motionLevel;
    state.colorPalette = static_cast<uint8_t>(_colorPalette);
    state.selectedPaletteIndex = _selectedPaletteIndex;
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

  bool ControllerFeatures::saveMotionLevel()
  {
    Preferences preferences;
    if (!preferences.begin("blehid", false))
    {
      Serial.println("[controller] failed to open NVS to save motion speed");
      return false;
    }
    const size_t written = preferences.putUChar("motion", _motionLevel);
    preferences.end();
    if (written != sizeof(uint8_t))
    {
      Serial.println("[controller] failed to persist motion speed");
      return false;
    }
    return true;
  }

  bool ControllerFeatures::saveMotionFlips()
  {
    Preferences preferences;
    if (!preferences.begin("blehid", false))
    {
      Serial.println("[controller] failed to open NVS to save motion flips");
      return false;
    }
    const size_t writtenX = preferences.putUChar("mflipX", _motionFlipX ? 1 : 0);
    const size_t writtenY = preferences.putUChar("mflipY", _motionFlipY ? 1 : 0);
    preferences.end();
    if (writtenX != sizeof(uint8_t) || writtenY != sizeof(uint8_t))
    {
      Serial.println("[controller] failed to persist motion flips");
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

  bool ControllerFeatures::saveColorPalette(ColorPalette palette)
  {
    Preferences preferences;
    if (!preferences.begin("blehid", false))
    {
      Serial.println("[controller] failed to open NVS to save color palette");
      return false;
    }
    const size_t written = preferences.putUChar("palette", static_cast<uint8_t>(palette));
    preferences.end();
    if (written != sizeof(uint8_t))
    {
      Serial.println("[controller] failed to persist color palette");
      return false;
    }
    return true;
  }

  bool ControllerFeatures::saveBrightness()
  {
    Preferences preferences;
    if (!preferences.begin("blehid", false))
    {
      Serial.println("[controller] failed to open NVS to save brightness");
      return false;
    }
    const size_t written = preferences.putUChar("brightness", _brightness);
    preferences.end();
    if (written != sizeof(uint8_t))
    {
      Serial.println("[controller] failed to persist brightness");
      return false;
    }
    return true;
  }

  bool ControllerFeatures::saveSleepTimeout()
  {
    Preferences preferences;
    if (!preferences.begin("blehid", false))
    {
      Serial.println("[controller] failed to open NVS to save sleep timeout");
      return false;
    }
    const size_t written = preferences.putUInt("sleepMs", _sleepTimeoutMs);
    preferences.end();
    if (written != sizeof(uint32_t))
    {
      Serial.println("[controller] failed to persist sleep timeout");
      return false;
    }
    return true;
  }

  ControllerFeatures::ColorPalette ControllerFeatures::loadColorPalette()
  {
    Preferences preferences;
    if (!preferences.begin("blehid", true))
    {
      Serial.println("[controller] failed to open NVS to load color palette");
      return ColorPalette::Default;
    }
    const uint8_t saved = preferences.getUChar("palette", 0);
    preferences.end();
    if (saved < static_cast<uint8_t>(kPaletteCount))
    {
      return static_cast<ColorPalette>(saved);
    }
    return ColorPalette::Default;
  }

  void ControllerFeatures::applyMode(Mode mode)
  {
    if (mode == _mode)
    {
      return;
    }
    if (_mode == Mode::Brightness && !_brightnessSaved)
    {
      _brightnessSaved = saveBrightness();
    }
    if (_mode == Mode::Sleep)
    {
      saveSleepTimeout();
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
    _haveMouseReport = false;
    _motionVelocityX = 0.0f;
    _motionVelocityY = 0.0f;
    _motionRemainderX = 0.0f;
    _motionRemainderY = 0.0f;
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
      _motionSaved = true;
      _dpiStickCentered = true;
      _motionStickCentered = true;
    }
    if (_mode == Mode::AirMouse)
    {
      _motionSaved = true;
      _motionFlipSaved = true;
      _airMouseStickCentered = true;
    }
    if (_mode == Mode::Magnifier)
    {
      _lastMagnifierZoomAt = millis();
      _lastMagnifierPanAt = millis();
    }
    if (_mode == Mode::Style)
    {
      _styleSaved = true;
      _selectedPaletteIndex = static_cast<uint8_t>(_colorPalette);
      _lastStyleInputAt = millis();
    }
    if (_mode == Mode::Brightness)
    {
      _brightnessSaved = true;
      _brightnessStickCentered = true;
      _lastBrightnessAdjustAt = millis();
      M5.Display.setBrightness(_brightness);
    }
    if (_mode == Mode::Sleep)
    {
      _sleepStickCentered = true;
      _lastSleepAdjustAt = millis();
      _brightnessSaved = true;
    }
    _orientationInitialized = false;
    _yaw = 0.0f;
    if (!saveMode(_mode))
    {
      Serial.println("[controller] mode changed for this session only");
    }
    Serial.printf("[controller] mode: %s\n", modeName(_mode));
  }

  void ControllerFeatures::toggleMicrophoneMode()
  {
    const bool enterMic =
        _mode != Mode::Microphone;
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

  void ControllerFeatures::toggleMotionFlip(bool flipX)
  {
    bool &flip = flipX ? _motionFlipX : _motionFlipY;
    flip = !flip;
    _motionFlipSaved = saveMotionFlips();
    _motionVelocityX = 0.0f;
    _motionVelocityY = 0.0f;
    _motionRemainderX = 0.0f;
    _motionRemainderY = 0.0f;
    Serial.printf("[controller] motion flip: X=%u Y=%u (%s)\n",
                  static_cast<unsigned>(_motionFlipX),
                  static_cast<unsigned>(_motionFlipY),
                  _motionFlipSaved ? "saved" : "not saved");
  }

  void ControllerFeatures::adjustMotionLevel(int32_t direction, uint32_t now)
  {
    if (direction > 0 && _motionLevel < 6)
    {
      ++_motionLevel;
    }
    else if (direction < 0 && _motionLevel > 0)
    {
      --_motionLevel;
    }
    else
    {
      return;
    }
    _lastMotionAdjustAt = now;
    _motionSaved = saveMotionLevel();
    Serial.printf("[controller] motion speed level %u (%s)\n",
                  static_cast<unsigned>(_motionLevel),
                  _motionSaved ? "saved" : "not saved");
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
          (_mode == Mode::Microphone) &&
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
      else if (_mode == Mode::AirMouse)
      {
        toggleMotionFlip(true);
      }
      else if (_mode == Mode::Magnifier)
      {
        zoomMagnifier(true);
      }
      else if (_mode == Mode::Brightness)
      {
        _brightness = 255;
        M5.Display.setBrightness(_brightness);
        _brightnessSaved = false;
      }
      else if (_mode == Mode::Sleep)
      {
        enterDeepSleep();
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
      else if (_mode == Mode::AirMouse)
      {
        toggleMotionFlip(false);
      }
      else if (_mode == Mode::Magnifier)
      {
        zoomMagnifier(false);
      }
      else if (_mode == Mode::Brightness)
      {
        _brightness = 64;
        M5.Display.setBrightness(_brightness);
        _brightnessSaved = false;
        Serial.println("[controller] brightness eco");
      }
      else if (_mode == Mode::Sleep)
      {
        _sleepTimeoutMs = 60000;
        _selectedSleepIndex = 1;
        Serial.println("[controller] sleep timeout reset");
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

    if (buttonA || buttonB || buttonA != _previousButtonA ||
        buttonB != _previousButtonB)
    {
      _lastValidInputAt = now;
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
    if (std::abs(joystick.x) >= kIdleMovementThreshold ||
        std::abs(joystick.y) >= kIdleMovementThreshold ||
        joystick.buttonPressed)
    {
      _lastValidInputAt = now;
    }
    if (now - _lastJoystickLogAt >= 1000)
    {
      _lastJoystickLogAt = now;
      Serial.printf("[joystick] x=%d y=%d button=%s mode=%s\n", joystick.x,
                    joystick.y, joystick.buttonPressed ? "pressed" : "released",
                    modeName(_mode));
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
      _selectedMode = modeOrderIndex(_mode);
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
            (_selectedMode + (y > 0 ? 1 : kPickerModeCount - 1)) % kPickerModeCount);
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
          const Mode selected = modeAtOrderIndex(_selectedMode);
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
      handleOrientationMode(joystick, now);
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
      handleDpiMode(x, y, now, joystick);
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

    if (_mode == Mode::AirMouse)
    {
      handleAirMouseJoystick(x, y, now);
      return;
    }

    if (_mode == Mode::Magnifier)
    {
      handleMagnifierJoystick(x, y, now);
      return;
    }

    if (_mode == Mode::Style)
    {
      handleStyleMode(now, joystick);
      return;
    }

    if (_mode == Mode::Brightness)
    {
      handleBrightnessMode(x, now, joystick);
      return;
    }

    if (_mode == Mode::Sleep)
    {
      handleSleepMode(x, now, joystick);
      return;
    }

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

    if (std::fabs(gx) > 5.0f || std::fabs(gy) > 5.0f ||
        std::fabs(gz) > 5.0f)
    {
      _lastValidInputAt = now;
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

    if (_modePickerActive)
    {
      // Selecting a mode must not move the pointer or pan the Magnifier.
      return;
    }

    if (_mode == Mode::AirMouse)
    {
      handleAirMouse(gyroX, gz, dt);
    }
    else if (_mode == Mode::Magnifier)
    {
      handleMagnifierMotion(gyroX, gz, dt, now);
    }
    else if (_mode != Mode::Orientation)
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
      // Front (tilt away, gyroX > 0) = accept (Enter), back (tilt toward, gyroX < 0) = cancel (Escape)
      const uint8_t key = gyroX < 0.0f ? kKeyEnter : kKeyEscape;
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
    if (_mode == Mode::Magnifier)
    {
      if (doubleClick)
      {
        _hid.sendHotkey(kModifierControlAlt, kKeyM);
        Serial.println("[magnifier] cycle view");
      }
      else
      {
        toggleMagnifier();
      }
      return;
    }

    if (_mode == Mode::Navigation)
    {
      // Double click = accept (Enter), single click = cancel (Escape)
      const uint8_t key = doubleClick ? kKeyEnter : kKeyEscape;
      _hid.sendKey(key);
      Serial.printf("[controller] navigation %s -> %s\n",
                    doubleClick ? "double-click" : "click",
                    doubleClick ? "Enter" : "Escape");
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

  void ControllerFeatures::enterDeepSleep()
  {
    Serial.println("[controller] entering deep sleep after idle timeout");
    _lastValidInputAt = millis();

    // Give the HID state a moment to settle, then power down the display.
    M5.Display.sleep();
    M5.Display.waitDisplay();
    M5.Speaker.end();

    // Buttons are on GPIO 11 / 12 on the StickS3 and are active low.
    constexpr uint64_t kButtonWakeMask =
        (1ULL << GPIO_NUM_11) | (1ULL << GPIO_NUM_12);
#if defined(ESP_IDF_VERSION_VAL) && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    esp_sleep_enable_ext1_wakeup_io(kButtonWakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
#else
    esp_sleep_enable_ext1_wakeup(kButtonWakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif

    // Allow the joystick to be re-polled periodically after wake, but the device
    // will reboot into the normal idle handling on any wake source.
    /* NOTE: joystick movement itself cannot wake the ESP32 from deep sleep
       because the Joystick2 unit is polled over I2C. The button wake sources
       above cover the explicit "button pressed/clicked" requirement. */
    esp_deep_sleep_start();
  }

} // namespace controller
