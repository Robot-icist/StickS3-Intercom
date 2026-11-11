#pragma once

#include <Arduino.h>
#include <algorithm>
#include <cmath>

namespace
{
constexpr uint8_t kJoystickAddress = 0x63;
constexpr uint8_t kJoystickOffsetRegister = 0x50;
constexpr uint8_t kJoystickButtonRegister = 0x20;
constexpr uint32_t kI2cFrequency = 100000;
constexpr uint8_t kJoystickProbeAttempts = 12;
constexpr uint32_t kJoystickProbeDelayMs = 100;
// The Joystick2's STM32G030 firmware
// (m5stack/M5Unit-Joystick2-Internal-FW) arms a "stop timeout" latch on every
// write address match and only clears it in its STOP interrupt. A missed STOP
// makes it de-init and re-init its I2C peripheral every ~510 ms, NACKing
// inside each re-init window, and it never clears the controller error flags.
// Register reads are therefore split into two STOP-terminated transfers so the
// latch is never left armed across the read phase. If the unit still wedges,
// recovery escalates from a bus free, to the firmware's reboot register, to a
// real Grove 5V power cycle.
constexpr uint8_t kJoystickRecoveryThreshold = 5;
constexpr uint32_t kJoystickRecoveryIntervalMs = 2000;
constexpr uint32_t kJoystickRecoveryMaxBackoffMs = 30000;
constexpr uint32_t kJoystickInterPhaseDelayUs = 250;
constexpr uint8_t kJoystickRebootRegister = 0xFD;
constexpr uint8_t kJoystickRebootValue = 0x01;
constexpr uint8_t kJoystickFirmwareVersionRegister = 0xFE;
constexpr uint8_t kJoystickAddressRegister = 0xFF;
constexpr uint32_t kJoystickRebootSettleMs = 800;
constexpr uint32_t kJoystickAddressRestoreMs = 400;
constexpr uint32_t kJoystickPowerCycleOffMs = 500;
constexpr uint32_t kJoystickPowerCycleSettleMs = 700;
constexpr uint8_t kJoystickRecoveryProbeAttempts = 12;
constexpr uint32_t kInputIntervalMs = 20;
#ifndef INTERCOM_IDLE_SLEEP_MS
#define INTERCOM_IDLE_SLEEP_MS 60000
#endif
constexpr uint32_t kDefaultIdleSleepMs = INTERCOM_IDLE_SLEEP_MS;
constexpr int32_t kIdleMovementThreshold = 200;
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
constexpr uint8_t kKeyM = 0x10;
constexpr uint8_t kKeyEnter = 0x28;
constexpr uint8_t kKeyEscape = 0x29;
constexpr uint8_t kKeyPageUp = 0x4B;
constexpr uint8_t kKeyArrowRight = 0x4F;
constexpr uint8_t kKeyArrowLeft = 0x50;
constexpr uint8_t kKeyArrowDown = 0x51;
constexpr uint8_t kKeyArrowUp = 0x52;
constexpr uint8_t kKeyPageDown = 0x4E;
constexpr uint8_t kKeypadMinus = 0x56;
constexpr uint8_t kKeypadPlus = 0x57;
constexpr uint8_t kModifierLeftControl = 0x01;
constexpr uint8_t kModifierLeftAlt = 0x04;
constexpr uint8_t kModifierLeftGui = 0x08;
constexpr uint8_t kModifierControlAlt =
    kModifierLeftControl | kModifierLeftAlt;

constexpr uint32_t kMagnifierZoomRepeatMs = 220;
constexpr uint32_t kMagnifierPanRepeatMs = 130;
constexpr float kMagnifierPanMinDps = 12.0f;

constexpr float kAirMouseDeadzoneDps = 2.0f;
constexpr float kAirMouseSmoothingSeconds = 0.045f;
constexpr float kAirMouseStopDps = 0.2f;
constexpr float kAirMouseBoostDps = 300.0f;
constexpr float kMotionScales[] = {5.0f, 8.0f, 12.0f, 18.0f,
                                   26.0f, 36.0f, 50.0f};
constexpr uint32_t kMotionAdjustRepeatMs = 300;

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

float removeDeadzone(float value, float deadzone)
{
  const float magnitude = fabsf(value);
  if (magnitude <= deadzone)
  {
    return 0.0f;
  }
  return value > 0.0f ? magnitude - deadzone : deadzone - magnitude;
}

int8_t clampMouseDelta(float value)
{
  if (value > 127.0f)
  {
    return 127;
  }
  if (value < -127.0f)
  {
    return -127;
  }
  return static_cast<int8_t>(value);
}
} // namespace