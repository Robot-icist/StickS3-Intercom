#include "ControllerFeatures.h"
#include "ControllerConstants.h"

#include <M5Unified.h>

#include <cstdio>

namespace controller
{
  void ControllerFeatures::handleBrightnessMode(int32_t x, int32_t y, uint32_t now, JoystickState &joystick)
  {
    // Stick up/down selects one of the four settings.
    if (std::abs(y) < kJoystickCenterThreshold)
    {
      _brightnessItemStickCentered = true;
    }
    else if (_brightnessItemStickCentered &&
             std::abs(y) >= kJoystickOrientationThreshold &&
             now - _lastBrightnessItemAt >= 250)
    {
      _brightnessItem = static_cast<uint8_t>(
          (_brightnessItem + (y > 0 ? kBrightnessItemCount - 1 : 1)) %
          kBrightnessItemCount);
      _brightnessItemStickCentered = false;
      _lastBrightnessItemAt = now;
      Serial.printf("[controller] brightness item %u\n",
                    static_cast<unsigned>(_brightnessItem));
    }

    // Stick left/right changes the selected setting.
    if (std::abs(x) < kJoystickCenterThreshold)
    {
      _brightnessStickCentered = true;
    }
    else if (_brightnessStickCentered &&
             std::abs(x) >= kJoystickOrientationThreshold &&
             now - _lastBrightnessAdjustAt >= 250)
    {
      bool changed = false;
      switch (_brightnessItem)
      {
      case 0: // Screen brightness
        if (x > 0 && _brightness < 240)
        {
          _brightness += 16;
          changed = true;
        }
        else if (x < 0 && _brightness > 16)
        {
          _brightness -= 16;
          changed = true;
        }
        if (changed)
        {
          M5.Display.setBrightness(_brightness);
        }
        break;
      case 1: // Joystick LED brightness
        if (x > 0 && _ledBrightness < 240)
        {
          _ledBrightness += 16;
          changed = true;
        }
        else if (x < 0 && _ledBrightness > 0)
        {
          _ledBrightness -= 16;
          changed = true;
        }
        break;
      case 2: // Idle color
        _ledIdleColorIndex = static_cast<uint8_t>(
            (_ledIdleColorIndex +
             (x > 0 ? 1 : kJoystickRgbIdleColorCount - 1)) %
            kJoystickRgbIdleColorCount);
        changed = true;
        break;
      default: // Color cycling on/off
        _ledColorCycle = x > 0;
        changed = true;
        break;
      }

      _brightnessStickCentered = false;
      _lastBrightnessAdjustAt = now;
      if (changed)
      {
        _brightnessSaved = false;
        Serial.printf(
            "[controller] screen=%u led=%u color=%u cycle=%s\n",
            static_cast<unsigned>(_brightness),
            static_cast<unsigned>(_ledBrightness),
            static_cast<unsigned>(_ledIdleColorIndex),
            _ledColorCycle ? "on" : "off");
      }
    }

    if (!joystick.buttonPressed && _lastJoystickButton &&
        !_joystickHoldHandled && now - _joystickPressedAt <= kClickMaxMs)
    {
      const bool screenSaved = saveBrightness();
      const bool ledSaved = saveJoystickRgbSettings();
      _brightnessSaved = screenSaved && ledSaved;
      Serial.printf("[controller] brightness saved: %s\n",
                    _brightnessSaved ? "yes" : "no");
    }
    _lastJoystickButton = joystick.buttonPressed;
    _haveMouseReport = false;
  }
} // namespace controller
