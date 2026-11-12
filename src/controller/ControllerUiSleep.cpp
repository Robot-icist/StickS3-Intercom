#include "ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller
{
  void ControllerUi::renderSleepMode(const ControllerViewState &state, uint32_t now, const Palette &p)
  {
    _screen.fillRoundRect(6, 48, _screen.width() - 12, 112, 7, p.panel);
    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("IDLE SLEEP", _screen.width() / 2, 58);
    char line[32];
    if (state.sleepTimeoutMs == 0)
    {
      snprintf(line, sizeof(line), "DISABLED");
    }
    else
    {
      snprintf(line, sizeof(line), "%u SEC", static_cast<unsigned>(state.sleepTimeoutMs / 1000));
    }
    _screen.setFont(&fonts::Font2);
    _screen.setTextColor(p.primary, p.panel);
    _screen.drawString(line, _screen.width() / 2, 82);
    _screen.setFont(&fonts::Font0);
    drawClippedMarquee(_screen, "Stick L/R: interval", 8, 165, _screen.width() - 16, 12, p.primary, p.background, now);
    drawClippedMarquee(_screen, "BtnA: sleep now  BtnB: reset", 8, 181, _screen.width() - 16, 12, p.primary, p.background, now);
    drawClippedMarquee(_screen, "Click: save | hold stick: select mode", 8, 199, _screen.width() - 16, 12, p.secondary, p.background, now);
    return;
  
  }
}
