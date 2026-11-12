#include "ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller
{
  void ControllerUi::renderDpiMode(const ControllerViewState &state, uint32_t now, const Palette &p)
  {
    _screen.fillRoundRect(6, 48, _screen.width() - 12, 112, 7, p.panel);
    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("POINTER / MOTION", _screen.width() / 2, 58);
    char level[24];
    snprintf(level, sizeof(level), "STICK  %u / 7",
             static_cast<unsigned>(state.dpiLevel + 1));
    _screen.setTextColor(p.primary, p.panel);
    _screen.drawString(level, _screen.width() / 2, 80);
    snprintf(level, sizeof(level), "MOTION %u / 7",
             static_cast<unsigned>(state.motionLevel + 1));
    _screen.drawString(level, _screen.width() / 2, 98);
    _screen.setTextColor((state.dpiSaved && state.motionSaved)
                             ? p.secondary
                             : p.warning,
                         p.panel);
    _screen.drawString((state.dpiSaved && state.motionSaved)
                           ? "SAVED"
                           : "NOT SAVED",
                       _screen.width() / 2, 120);
    drawClippedMarquee(_screen, "Stick L/R: pointer speed", 8, 165,
                       _screen.width() - 16, 12, p.primary,
                       p.background, now);
    drawClippedMarquee(_screen, "Stick U/D: motion speed", 8, 181,
                       _screen.width() - 16, 12, p.primary,
                       p.background, now);
    drawClippedMarquee(_screen, "Click: save | hold stick: select mode", 8, 199,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    drawClippedMarquee(_screen, "A: copy  B: paste | hold A: mic", 8, 217,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    if (state.joystickError)
    {
      drawClippedMarquee(_screen, "Joystick I2C error", 8, 233,
                         _screen.width() - 16, 12, p.error,
                         p.background, now);
    }
    return;
  
  }
}
