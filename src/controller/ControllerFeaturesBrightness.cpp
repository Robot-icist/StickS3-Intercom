#include "ControllerFeatures.h"
#include "ControllerConstants.h"

#include <M5Unified.h>

#include <cstdio>

namespace controller
{
  void ControllerFeatures::handleBrightnessMode(int32_t x, uint32_t now, JoystickState &joystick)
  {
    if (std::abs(x) < kJoystickCenterThreshold)
    {
      _brightnessStickCentered = true;
    }
    else if (_brightnessStickCentered &&
             std::abs(x) >= kJoystickOrientationThreshold &&
             now - _lastBrightnessAdjustAt >= 250)
    {
      if (x > 0 && _brightness <= 240)
      {
        _brightness += 16;
      }
      else if (x < 0 && _brightness >= 16)
      {
        _brightness -= 16;
      }
      _brightnessStickCentered = false;
      _lastBrightnessAdjustAt = now;
      M5.Display.setBrightness(_brightness);
      _brightnessSaved = false;
      Serial.printf("[controller] brightness %u\n", static_cast<unsigned>(_brightness));
    }

    if (!joystick.buttonPressed && _lastJoystickButton &&
        !_joystickHoldHandled && now - _joystickPressedAt <= kClickMaxMs)
    {
      _brightnessSaved = saveBrightness();
      Serial.printf("[controller] brightness saved: %s\n",
                    _brightnessSaved ? "yes" : "no");
    }
    _lastJoystickButton = joystick.buttonPressed;
    _haveMouseReport = false;
  }
} // namespace controller