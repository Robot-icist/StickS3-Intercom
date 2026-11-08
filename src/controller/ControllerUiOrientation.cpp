#include "ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller
{
constexpr float kRadToDeg = 57.2957795f;

void ControllerUi::renderImuCube(const ControllerViewState &state, const Palette &p)
{
  constexpr float halfSize = 1.0f;
  constexpr float scale = 37.0f;
  constexpr float centerX = 67.5f;
  constexpr float centerY = 126.0f;
  const float sinRoll = sinf(state.roll);
  const float cosRoll = cosf(state.roll);
  const float sinPitch = sinf(state.pitch);
  const float cosPitch = cosf(state.pitch);
  const float sinYaw = sinf(state.yaw);
  const float cosYaw = cosf(state.yaw);

  _screen.fillScreen(p.background);
  _screen.setTextColor(p.primary, p.background);
  _screen.setTextDatum(lgfx::textdatum_t::top_left);
  _screen.setFont(&fonts::Font0);
  _screen.drawString("ORIENTATION", 8, 8);
  drawBatteryStatus(_screen, state.batteryLevel, 8, p);
  drawNetworkIdentity(_screen, state, millis(), p);
  char baseOrientation[32];
  snprintf(baseOrientation, sizeof(baseOrientation), "BASE %u DEG %s",
           static_cast<unsigned>(state.pendingBaseRotation * 90),
           state.orientationSaved ? "SAVED" : "UNSAVED");
  _screen.setTextDatum(lgfx::textdatum_t::top_center);
  _screen.setTextColor(state.orientationSaved ? p.secondary : p.warning,
                       p.background);
  _screen.drawString(baseOrientation, _screen.width() / 2, 32);

  char orientation[40];
  snprintf(orientation, sizeof(orientation), "R %.0f   P %.0f",
           state.roll * kRadToDeg, state.pitch * kRadToDeg);
  _screen.setTextColor(p.primary, p.background);
  _screen.drawString(orientation, _screen.width() / 2, 43);
  _screen.drawRoundRect(8, 54, _screen.width() - 16, 146, 10, p.border);

  int16_t points[8][2] = {};
  for (uint8_t i = 0; i < 8; ++i)
  {
    const float x = (i & 0x01) ? halfSize : -halfSize;
    const float y = (i & 0x02) ? halfSize : -halfSize;
    const float z = (i & 0x04) ? halfSize : -halfSize;
    const float rollX = x;
    const float rollY = y * cosRoll - z * sinRoll;
    const float rollZ = y * sinRoll + z * cosRoll;
    const float pitchX = rollX * cosPitch + rollZ * sinPitch;
    const float pitchY = rollY;
    const float yawX = pitchX * cosYaw - pitchY * sinYaw;
    const float yawY = pitchX * sinYaw + pitchY * cosYaw;
    points[i][0] = static_cast<int16_t>(centerX + scale * yawX);
    points[i][1] = static_cast<int16_t>(centerY - scale * yawY);
  }

  for (uint8_t i = 0; i < 8; ++i)
  {
    for (uint8_t bit = 0; bit < 3; ++bit)
    {
      const uint8_t j = i ^ (1U << bit);
      if (i < j)
      {
        _screen.drawLine(points[i][0], points[i][1], points[j][0], points[j][1],
                         bit == 2 ? p.accent : p.primary);
      }
    }
  }

  // Draw the front face (z = -halfSize, indices 0, 1, 2, 3)
  _screen.fillTriangle(points[0][0], points[0][1], points[1][0], points[1][1],
                       points[2][0], points[2][1], p.secondary);
  _screen.fillTriangle(points[0][0], points[0][1], points[2][0], points[2][1],
                       points[3][0], points[3][1], p.secondary);

  // Draw the back face (z = +halfSize, indices 4, 5, 6, 7)
  _screen.fillTriangle(points[4][0], points[4][1], points[5][0], points[5][1],
                       points[6][0], points[6][1], p.primary);
  _screen.fillTriangle(points[4][0], points[4][1], points[6][0], points[6][1],
                       points[7][0], points[7][1], p.primary);

  _screen.setTextDatum(lgfx::textdatum_t::top_center);
  _screen.setTextColor(p.secondary, p.background);
  _screen.drawString("Front/Enter: accept | Back/Esc: cancel", _screen.width() / 2, 224);
  _screen.pushSprite(0, 0);
}

}
