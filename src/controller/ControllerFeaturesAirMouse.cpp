#include "ControllerFeatures.h"
#include "ControllerConstants.h"

#include "BleHidDevice.h"

#include <M5Unified.h>

#include <cstdio>

namespace controller
{
  void ControllerFeatures::updateMotionRates(float gyroX, float gyroZ, float dt)
  {
    // Horizontal movement comes from yaw; vertical movement comes from the
    // base-rotated pitch axis. The same filtered rates feed both Air Mouse and
    // Magnifier panning so their feel stays consistent.
    float rawX = gyroZ;
    float rawY = -gyroX;
    if (_motionFlipX)
    {
      rawX = -rawX;
    }
    if (_motionFlipY)
    {
      rawY = -rawY;
    }
    rawX = removeDeadzone(rawX, kAirMouseDeadzoneDps);
    rawY = removeDeadzone(rawY, kAirMouseDeadzoneDps);

    const float alpha = 1.0f - expf(-dt / kAirMouseSmoothingSeconds);
    _motionVelocityX += alpha * (rawX - _motionVelocityX);
    _motionVelocityY += alpha * (rawY - _motionVelocityY);
    if (fabsf(_motionVelocityX) < kAirMouseStopDps)
    {
      _motionVelocityX = 0.0f;
    }
    if (fabsf(_motionVelocityY) < kAirMouseStopDps)
    {
      _motionVelocityY = 0.0f;
    }
  }

  void ControllerFeatures::handleAirMouse(float gyroX, float gyroZ, float dt)
  {
    updateMotionRates(gyroX, gyroZ, dt);

    const float speed = sqrtf(_motionVelocityX * _motionVelocityX +
                              _motionVelocityY * _motionVelocityY);
    // Fast flicks get a gentle boost so a wrist turn can cross the whole
    // desktop, while small movements stay precise.
    const float boost =
        1.0f + std::min(speed, kAirMouseBoostDps) / kAirMouseBoostDps;
    const float scale =
        kMotionScales[std::min<uint8_t>(_motionLevel, 6)] * boost;

    _motionRemainderX += _motionVelocityX * scale * dt;
    _motionRemainderY += _motionVelocityY * scale * dt;
    _motionRemainderX =
        std::max(-254.0f, std::min(254.0f, _motionRemainderX));
    _motionRemainderY =
        std::max(-254.0f, std::min(254.0f, _motionRemainderY));

    const int8_t dx = clampMouseDelta(_motionRemainderX);
    const int8_t dy = clampMouseDelta(_motionRemainderY);
    if (dx != 0 || dy != 0 || !_haveMouseReport)
    {
      _hid.sendMouseReport(0, dx, dy);
      _haveMouseReport = true;
      _motionRemainderX -= dx;
      _motionRemainderY -= dy;
    }
  }

  void ControllerFeatures::handleAirMouseJoystick(int32_t x, int32_t y,
                                                  uint32_t now)
  {
    (void)x;
    const int32_t magnitude = std::abs(y);
    if (magnitude < kJoystickCenterThreshold)
    {
      _airMouseStickCentered = true;
      return;
    }
    if (magnitude < kJoystickOrientationThreshold)
    {
      return;
    }

    // Change one level per deflection, then repeat while the stick is held.
    if (_airMouseStickCentered ||
        now - _lastMotionAdjustAt >= kMotionAdjustRepeatMs)
    {
      adjustMotionLevel(y < 0 ? 1 : -1, now);
      _airMouseStickCentered = false;
      _lastMotionAdjustAt = now;
    }
  }


} // namespace controller
