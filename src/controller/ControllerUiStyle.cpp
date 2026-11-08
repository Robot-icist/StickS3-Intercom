#include "ControllerUi.h"

#include <cmath>
#include <cstdio>

namespace controller
{
  void ControllerUi::renderStyleMode(const ControllerViewState &state, uint32_t now, const Palette &p)
  {
    _screen.fillRoundRect(6, 48, _screen.width() - 12, 120, 7, p.panel);
    _screen.setTextDatum(lgfx::textdatum_t::top_center);
    _screen.setFont(&fonts::Font0);
    _screen.setTextColor(p.accent, p.panel);
    _screen.drawString("STYLE - COLOR PALETTE", _screen.width() / 2, 60);

    static const char *const paletteNames[] = {
        "Default", "Dark", "Light", "High Contrast",
        "Cyberpunk", "Monochrome", "Sepia", "Forest"};
    constexpr uint8_t kPaletteCount = 8;

    _screen.setFont(&fonts::Font0);
    for (uint8_t i = 0; i < kPaletteCount; ++i)
    {
      const int16_t y = 82 + i * 14;
      const bool isCurrent = i == state.colorPalette;
      const bool isSelected = i == state.selectedPaletteIndex;
      // Show selected palette with different indicator
      _screen.setTextColor(isCurrent ? p.accent : (isSelected ? p.warning : p.secondary),
                           p.panel);
      char line[32];
      snprintf(line, sizeof(line), "%s%s%s", paletteNames[i],
               isCurrent ? " *" : "", isSelected && !isCurrent ? " >" : "");
      _screen.drawString(line, _screen.width() / 2, y);
    }

    _screen.setTextColor(p.primary, p.background);
    drawClippedMarquee(_screen, "Stick L/R: cycle palettes", 8, 190,
                       _screen.width() - 16, 12, p.primary,
                       p.background, now);
    drawClippedMarquee(_screen, "Click: save palette", 8, 208,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    drawClippedMarquee(_screen, "Hold stick: select mode", 8, 226,
                       _screen.width() - 16, 12, p.secondary,
                       p.background, now);
    _screen.pushSprite(0, 0);
    return;
  
  }
}
