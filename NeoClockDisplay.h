#ifndef _NEOCLOCKDISPLAY_H_
#define _NEOCLOCKDISPLAY_H_
#include <Arduino.h>

// Thin abstraction over "something that can show NUM_LEDS colors". The only
// implementation today (NeoPixelStripDisplay) wraps Adafruit_NeoPixel, but the
// clock-face/status/alert rendering code only ever talks to this interface, so
// a future mock implementation could exercise that rendering code without any
// real hardware attached.
class NeoClockDisplay {
public:
  virtual ~NeoClockDisplay() {}
  virtual void begin() = 0;
  virtual void clear() = 0;
  virtual void setPixel(int index, uint32_t rgb) = 0;
  virtual uint32_t getPixel(int index) = 0;
  virtual void show() = 0;
};

#endif
