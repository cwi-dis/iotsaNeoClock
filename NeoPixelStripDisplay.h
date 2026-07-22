#ifndef _NEOPIXELSTRIPDISPLAY_H_
#define _NEOPIXELSTRIPDISPLAY_H_
#include "NeoClockDisplay.h"
#include <Adafruit_NeoPixel.h>

// NeoClockDisplay implementation driving a real Adafruit_NeoPixel strip.
class NeoPixelStripDisplay : public NeoClockDisplay {
public:
  NeoPixelStripDisplay(int numLeds, int pin)
  : strip(numLeds, pin, NEO_GRB + NEO_KHZ800)
  {}
  void begin() override { strip.begin(); }
  void clear() override { strip.clear(); }
  void setPixel(int index, uint32_t rgb) override { strip.setPixelColor(index, rgb); }
  uint32_t getPixel(int index) override { return strip.getPixelColor(index); }
  void show() override { strip.show(); }
private:
  Adafruit_NeoPixel strip;
};

#endif
