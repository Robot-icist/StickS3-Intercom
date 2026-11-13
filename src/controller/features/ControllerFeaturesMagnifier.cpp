#include "../ControllerFeatures.h"
#include "../ControllerConstants.h"

#include "../BleHidDevice.h"

#include <M5Unified.h>

#include <cstdio>

namespace controller
{
  void ControllerFeatures::toggleMagnifier()
  {
    if (_magnifierOpen)
    {
      _hid.sendHotkey(kModifierLeftGui, kKeyEscape);
      _magnifierOpen = false;
      Serial.println("[magnifier] closed");
    }
    else
    {
      _hid.sendHotkey(kModifierLeftGui, kKeypadPlus);
      _magnifierOpen = true;
      _motionVelocityX = 0.0f;
      _motionVelocityY = 0.0f;
      _motionRemainderX = 0.0f;
      _motionRemainderY = 0.0f;
      _lastMagnifierPanAt = millis();
      Serial.println("[magnifier] opened");
    }
  }

  void ControllerFeatures::zoomMagnifier(bool zoomIn)
  {
    if (!_magnifierOpen && !zoomIn)
    {
      return;
    }
    _hid.sendHotkey(kModifierLeftGui,
                    zoomIn ? kKeypadPlus : kKeypadMinus);
    if (zoomIn)
    {
      _magnifierOpen = true;
    }
    Serial.printf("[magnifier] zoom %s\n", zoomIn ? "in" : "out");
  }

  void ControllerFeatures::panMagnifier(uint8_t key)
  {
    if (!_magnifierOpen)
    {
      return;
    }
    _hid.sendHotkey(kModifierControlAlt, key);
  }

  void ControllerFeatures::handleMagnifierJoystick(int32_t x, int32_t y,
                                                   uint32_t now)
  {
    const bool zoomIn = y <= -kJoystickOrientationThreshold;
    const bool zoomOut = y >= kJoystickOrientationThreshold;
    if ((zoomIn || zoomOut) &&
        now - _lastMagnifierZoomAt >= kMagnifierZoomRepeatMs)
    {
      zoomMagnifier(zoomIn);
      _lastMagnifierZoomAt = now;
    }

    const bool panLeft = x <= -kJoystickOrientationThreshold;
    const bool panRight = x >= kJoystickOrientationThreshold;
    if ((panLeft || panRight) &&
        now - _lastMagnifierPanAt >= kMagnifierPanRepeatMs)
    {
      panMagnifier(panLeft ? kKeyArrowLeft : kKeyArrowRight);
      _lastMagnifierPanAt = now;
    }
  }

  void ControllerFeatures::handleMagnifierMotion(float gyroX, float gyroZ,
                                                 float dt, uint32_t now)
  {
    updateMotionRates(gyroX, gyroZ, dt);
    if (!_magnifierOpen)
    {
      return;
    }

    const float magnitude =
        std::max(fabsf(_motionVelocityX), fabsf(_motionVelocityY));
    if (magnitude < kMagnifierPanMinDps)
    {
      return;
    }

    // Reuse the Air Mouse sensitivity and flips. Higher levels pan faster.
    const float levelScale =
        kMotionScales[std::min<uint8_t>(_motionLevel, 6)] / kMotionScales[3];
    const float effective = magnitude * levelScale;
    const float interval =
        std::max(70.0f, std::min(220.0f, 220.0f - effective * 0.55f));
    if (now - _lastMagnifierPanAt < static_cast<uint32_t>(interval))
    {
      return;
    }

    if (fabsf(_motionVelocityX) >= fabsf(_motionVelocityY))
    {
      panMagnifier(_motionVelocityX > 0.0f ? kKeyArrowRight : kKeyArrowLeft);
    }
    else
    {
      panMagnifier(_motionVelocityY > 0.0f ? kKeyArrowDown : kKeyArrowUp);
    }
    _lastMagnifierPanAt = now;
  }


} // namespace controller
