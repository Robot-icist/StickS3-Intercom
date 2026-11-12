#include "ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller
{
  void ControllerUi::renderMagnifierMode(const ControllerViewState &state, uint32_t now, const Palette &p)
  {
    _screen.fillRoundRect(6, 48, _screen.width() - 12, 100, 7, p.panel);
    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("WINDOWS MAGNIFIER", _screen.width() / 2, 60);
    _screen.setFont(&fonts::Font2);
    _screen.setTextColor(state.magnifierOpen ? p.connected : p.secondary,
                         p.panel);
    _screen.drawString(state.magnifierOpen ? "OPEN" : "CLOSED",
                       _screen.width() / 2, 82);
    _screen.setFont(&fonts::Font0);
    char motion[24];
    snprintf(motion, sizeof(motion), "MOTION LEVEL %u",
             static_cast<unsigned>(state.motionLevel + 1));
    _screen.setTextColor(p.secondary, p.panel);
    _screen.drawString(motion, _screen.width() / 2, 104);
    snprintf(motion, sizeof(motion), "X:%s  Y:%s",
             state.motionFlipX ? "FLIP" : "NORM",
             state.motionFlipY ? "FLIP" : "NORM");
    _screen.drawString(motion, _screen.width() / 2, 122);
    drawClippedMarquee(_screen, "Stick U/D: zoom", 8, 160,
                       _screen.width() - 16, 12, p.primary,
                       p.background, now);
    drawClippedMarquee(_screen, "Stick L/R or tilt: pan", 8, 178,
                       _screen.width() - 16, 12, p.primary,
                       p.background, now);
    drawClippedMarquee(_screen, "Click: open/close", 8, 196,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    drawClippedMarquee(_screen, "Double-click: cycle view", 8, 214,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    drawClippedMarquee(_screen, "A/B: zoom | motion: Air Mouse", 8, 232,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    return;
  
  }
}
