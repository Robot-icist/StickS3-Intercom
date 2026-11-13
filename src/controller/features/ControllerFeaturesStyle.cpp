#include "../ControllerFeatures.h"
#include "../ControllerConstants.h"

#include <M5Unified.h>

#include <cstdio>

namespace controller
{
  void ControllerFeatures::handleStyleMode(uint32_t now, JoystickState &joystick)
  {
    (void)joystick;
    JoystickState styleJoystick;
    if (readJoystick(styleJoystick))
    {
      int32_t sx = styleJoystick.x;
      int32_t sy = styleJoystick.y;

      static bool styleLastButton = false;
      static uint32_t stylePressedAt = 0;
      static bool styleHoldHandled = false;

      if (styleJoystick.buttonPressed && !styleLastButton)
      {
        stylePressedAt = now;
        styleHoldHandled = false;
      }

      // Use Y axis (up/down) for palette selection since palettes are listed vertically
      if (std::abs(sy) >= kJoystickOrientationThreshold)
      {
        if (_styleSaved)
        {
          if (sy < 0) // Up - previous palette
          {
            if (_selectedPaletteIndex == 0)
              _selectedPaletteIndex = static_cast<uint8_t>(kPaletteCount) - 1;
            else
              --_selectedPaletteIndex;
          }
          else // Down - next palette
          {
            _selectedPaletteIndex = (_selectedPaletteIndex + 1) % static_cast<uint8_t>(kPaletteCount);
          }
          _styleSaved = false;
          Serial.printf("[controller] selected palette: %s\n", paletteName(static_cast<ColorPalette>(_selectedPaletteIndex)));
        }
      }
      else
      {
        _styleSaved = true;
      }

      if (!styleJoystick.buttonPressed && styleLastButton &&
          !styleHoldHandled &&
          now - stylePressedAt <= kClickMaxMs)
      {
        _colorPalette = static_cast<ColorPalette>(_selectedPaletteIndex);
        _styleSaved = saveColorPalette(_colorPalette);
        Serial.printf("[controller] saved palette: %s (%s)\n",
                      paletteName(_colorPalette), _styleSaved ? "saved" : "not saved");
      }

      if (styleJoystick.buttonPressed && !styleHoldHandled &&
          now - stylePressedAt >= kModeHoldMs)
      {
        _modePickerActive = true;
        _selectedMode = modeOrderIndex(_mode);
        _modePickerStickCentered = std::abs(sx) < kJoystickCenterThreshold &&
                                   std::abs(sy) < kJoystickCenterThreshold;
        _modePickerIgnoreButtonRelease = true;
        _havePendingJoystickClick = false;
        styleHoldHandled = true;
        Serial.println("[controller] mode selection opened from style");
      }

      styleLastButton = styleJoystick.buttonPressed;
    }
    _haveMouseReport = false;
  }
} // namespace controller