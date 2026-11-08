#include "ControllerFeatures.h"
#include "ControllerConstants.h"

#include <M5Unified.h>

#include <cstdio>

namespace controller
{
  void ControllerFeatures::handleSleepMode(int32_t x, uint32_t now, JoystickState &joystick)
  {
    constexpr uint32_t kSleepOptions[] = {30000, 60000, 120000, 300000, 0};
    constexpr uint8_t kSleepOptionCount = sizeof(kSleepOptions) / sizeof(kSleepOptions[0]);

    if (std::abs(x) < kJoystickCenterThreshold)
    {
      _sleepStickCentered = true;
    }
    else if (_sleepStickCentered && std::abs(x) >= kJoystickOrientationThreshold &&
             now - _lastSleepAdjustAt >= 250)
    {
      if (x > 0 && _selectedSleepIndex < kSleepOptionCount - 1)
      {
        ++_selectedSleepIndex;
      }
      else if (x < 0 && _selectedSleepIndex > 0)
      {
        --_selectedSleepIndex;
      }
      _sleepTimeoutMs = kSleepOptions[_selectedSleepIndex];
      _sleepStickCentered = false;
      _lastSleepAdjustAt = now;
      Serial.printf("[controller] sleep timeout %u ms\n",
                    static_cast<unsigned>(_sleepTimeoutMs));
    }

    if (!joystick.buttonPressed && _lastJoystickButton &&
        !_joystickHoldHandled && now - _joystickPressedAt <= kClickMaxMs)
    {
      saveSleepTimeout();
    }
    _lastJoystickButton = joystick.buttonPressed;
    _haveMouseReport = false;
  }
} // namespace controller