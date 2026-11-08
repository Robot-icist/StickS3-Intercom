#include "ControllerFeatures.h"
#include "ControllerConstants.h"

#include <M5Unified.h>

#include <cstdio>

namespace controller
{
  void ControllerFeatures::handleOrientationMode(JoystickState &joystick, uint32_t now)
  {
    const int32_t x = joystick.x;
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
  }
} // namespace controller