#include "../ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller
{
  void ControllerUi::renderMouseMode(const ControllerViewState &state, uint32_t now, const Palette &p)
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
