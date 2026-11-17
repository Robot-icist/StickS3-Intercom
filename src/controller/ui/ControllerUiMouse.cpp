#include "../ControllerUi.h"
#include "../ControllerConstants.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace controller
{
  void ControllerUi::renderMouseMode(const ControllerViewState &state, uint32_t now, const Palette &p)
  {
  static float smoothX = 0.0f;
  static float smoothY = 0.0f;

  // Target uses the orientation-corrected stick values, so the on-screen
  // animation follows the same axes as pointer movement.
  float targetX = static_cast<float>(state.joystickX) / 2047.0f;
  float targetY = static_cast<float>(state.joystickY) / 2047.0f;
  if (targetX < -1.0f)
  {
    targetX = -1.0f;
  }
  if (targetX > 1.0f)
  {
    targetX = 1.0f;
  }
  if (targetY < -1.0f)
  {
    targetY = -1.0f;
  }
  if (targetY > 1.0f)
  {
    targetY = 1.0f;
  }

  // Keep the target on the unit circle so the dot can never leave the dial.
  const float targetMag = std::sqrt(targetX * targetX + targetY * targetY);
  if (targetMag > 1.0f)
  {
    targetX /= targetMag;
    targetY /= targetMag;
  }

  // Smooth the displayed position for a fluid, modern animation.
  smoothX += (targetX - smoothX) * 0.55f;
  smoothY += (targetY - smoothY) * 0.55f;

  const bool moving =
      std::abs(state.joystickX) >= kIdleMovementThreshold ||
      std::abs(state.joystickY) >= kIdleMovementThreshold;

  if (moving)
  {
    _screen.fillRoundRect(6, 40, _screen.width() - 12, 120, 10, p.panel);
    _screen.drawRoundRect(6, 40, _screen.width() - 12, 120, 10, p.border);

    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("JOYSTICK", _screen.width() / 2, 46);

    const int32_t cx = _screen.width() / 2;
    const int32_t cy = 104;
    int32_t radius = (_screen.width() - 30) / 2;
    if (radius > 50)
    {
      radius = 50;
    }
    if (radius < 26)
    {
      radius = 26;
    }

    // Subtle animated pulse ring.
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(now) * 0.006f);
    const int32_t pulseRadius = radius + static_cast<int32_t>(pulse * 4.0f);
    _screen.drawCircle(cx, cy, pulseRadius, p.secondary);

    // Dial track.
    _screen.drawCircle(cx, cy, radius, p.border);
    _screen.drawCircle(cx, cy, radius - 1, p.border);
    _screen.drawCircle(cx, cy, radius / 2, p.secondary);
    _screen.drawFastHLine(cx - radius, cy, radius * 2, p.secondary);
    _screen.drawFastVLine(cx, cy - radius, radius * 2, p.secondary);

    // Keep the smoothed point inside the dial.
    float smoothMag = std::sqrt(smoothX * smoothX + smoothY * smoothY);
    float drawX = smoothX;
    float drawY = smoothY;
    if (smoothMag > 1.0f)
    {
      drawX /= smoothMag;
      drawY /= smoothMag;
      smoothMag = 1.0f;
    }
    const int32_t drawRadius = radius - 6;
    const int32_t dx = cx + static_cast<int32_t>(drawX * static_cast<float>(drawRadius));
    const int32_t dy = cy - static_cast<int32_t>(drawY * static_cast<float>(drawRadius));

    // Trail from center to the stick position.
    _screen.drawLine(cx, cy, dx, dy, p.accent);
    _screen.drawLine(cx + 1, cy, dx + 1, dy, p.accent);

    // Glow + dot.
    _screen.drawCircle(dx, dy, 9, p.accent);
    _screen.drawCircle(dx, dy, 7, p.accent);
    _screen.fillCircle(dx, dy, 5, p.accent);
    _screen.fillCircle(dx, dy, 2, p.panel);

    // Readout.
    const float magnitude = smoothMag;
    char valueText[48];
    snprintf(valueText, sizeof(valueText), "X %+5d  Y %+5d  M %.2f",
             static_cast<int>(state.joystickX),
             static_cast<int>(state.joystickY),
             static_cast<double>(magnitude));
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(p.primary, p.panel);
    _screen.drawString(valueText, _screen.width() / 2, 148);
  }
  else
  {
    _screen.fillRoundRect(6, 42, _screen.width() - 12, 58, 7, p.panel);
    _screen.setTextDatum(lgfx::textdatum_t::top_left);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("JOYSTICK", 14, 47);
    if (state.navigationMode)
    {
      drawClippedMarquee(_screen, "Move: scroll", 14, 62,
                        _screen.width() - 28, 12, p.primary, p.panel,
                        now);
      drawClippedMarquee(_screen, "Click: Enter / dbl: Esc", 14, 78,
                        _screen.width() - 28, 12, p.primary, p.panel,
                        now);
    }
    else
    {
      drawClippedMarquee(_screen, "Move: pointer", 14, 62,
                        _screen.width() - 28, 12, p.primary, p.panel,
                        now);
      drawClippedMarquee(_screen, "Click: left / dbl: right", 14, 78,
                        _screen.width() - 28, 12, p.primary, p.panel,
                        now);
    }

    _screen.fillRoundRect(6, 108, _screen.width() - 12, 43, 7, p.panel);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("BUTTONS", 14, 114);
    drawClippedMarquee(
        _screen,
        state.navigationMode ? "A/B: page up / down" : "A: copy  B: paste", 14,
        130, _screen.width() - 28, 12, p.primary, p.panel, now);
  }

  _screen.setTextDatum(lgfx::textdatum_t::top_left);
  _screen.setFont(&fonts::Font0);
  drawClippedMarquee(_screen, "Gesture L/R: NO | F/B: YES", 8, 162,
                     _screen.width() - 16, 12, p.secondary,
                     p.background, now);
  drawClippedMarquee(_screen, "Stick hold: select mode", 8,
                     181, _screen.width() - 16, 12, p.secondary,
                     p.background, now);
  drawClippedMarquee(_screen, "Hold A: mic <-> last mode", 8, 198,
                     _screen.width() - 16, 12, p.secondary,
                     p.background, now);

  if (state.joystickError)
  {
    drawClippedMarquee(_screen, "Joystick I2C error", 8, 220,
                       _screen.width() - 16, 12, p.error, p.background,
                       now);
  }
  else if (now - state.lastValidInputAt > 1000)
  {
    drawClippedMarquee(_screen, "Waiting for joystick", 8, 220,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
  }
  }
}