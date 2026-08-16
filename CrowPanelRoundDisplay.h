#ifndef _CROWPANELROUNDDISPLAY_H_
#define _CROWPANELROUNDDISPLAY_H_
// LovyanGFX.hpp/lib_deps and the ESP32-SPI-host constants this class relies on
// are only available for the crowpanel128 env -- keep this whole file inert
// (nothing declared) for every other board, matching iotsa's own
// #ifdef-wrap-the-file convention for optional library code (e.g. iotsaBle.h).
#ifdef WITH_ROUNDLCD_DISPLAY
#include "NeoClockDisplay.h"
#include <LovyanGFX.hpp>
#include "PI4IOE5V6408.h"

// NeoClockDisplay implementation for the Elecrow CrowPanel ESP32-C3 1.28"
// round LCD (GC9A01 panel, 240x240, driven through LovyanGFX). Draws the same
// 60-dot ring the NeoPixel version shows, arranged clockwise starting at 12
// o'clock, redrawing only the dots that actually changed since the last
// show(). Pins, the I2C IO-expander bring-up sequence (LCD/touch reset,
// backlight enable) and the SPI panel config are all hardcoded for this
// specific board -- see doc/board-elecrow-crowpanel-1.28.md. There's only one
// round-LCD board target today; if a second one shows up, split the
// board-agnostic ring-drawing logic out into a base class then.
class CrowPanelRoundDisplay : public NeoClockDisplay {
public:
  CrowPanelRoundDisplay(int numLeds);
  void begin() override;
  void clear() override;
  void setPixel(int index, uint32_t rgb) override;
  uint32_t getPixel(int index) override;
  void show() override;
private:
  class LGFX : public lgfx::LGFX_Device {
  public:
    LGFX();
  private:
    lgfx::Panel_GC9A01 panel;
    lgfx::Bus_SPI bus;
  };

  void dotPosition(int index, int &x, int &y);

  LGFX tft;
  PI4IOE5V6408 expander;
  int numLeds;
  uint32_t *pending;    // colors set via setPixel(), not yet pushed to the panel
  uint32_t *lastShown;  // colors actually drawn on the panel as of the last show()
};

#endif // WITH_ROUNDLCD_DISPLAY
#endif
