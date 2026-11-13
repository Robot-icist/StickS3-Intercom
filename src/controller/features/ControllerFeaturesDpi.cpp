#include "../ControllerFeatures.h"
#include "../ControllerConstants.h"

#include <M5Unified.h>

#include <cstdio>

namespace controller
{
  void ControllerFeatures::handleDpiMode(int32_t x, int32_t y, uint32_t now, JoystickState &joystick)
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

      if (std::abs(y) < kJoystickCenterThreshold)
      {
        _motionStickCentered = true;
      }
      else if (_motionStickCentered &&
               std::abs(y) >= kJoystickOrientationThreshold &&
               now - _lastMotionAdjustAt >= 250)
      {
        adjustMotionLevel(y < 0 ? 1 : -1, now);
        _motionStickCentered = false;
      }

      if (!joystick.buttonPressed && _lastJoystickButton &&
          !_joystickHoldHandled && now - _joystickPressedAt <= kClickMaxMs)
      {
        _dpiSaved = saveDpiLevel();
        _motionSaved = saveMotionLevel();
      }
      _lastJoystickButton = joystick.buttonPressed;
      _haveMouseReport = false;
  }
} // namespace controller