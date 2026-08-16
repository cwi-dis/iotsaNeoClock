#include "CrowPanelRoundDisplay.h"
#ifdef WITH_ROUNDLCD_DISPLAY
#include <Wire.h>

#define I2C_SDA 4
#define I2C_SCL 5

#define EXPANDER_PIN_TOUCH_RESET 3
#define EXPANDER_PIN_LCD_RESET   4
#define EXPANDER_PIN_BACKLIGHT   2

// Physical layout (see readme.md): 12 radial 5-LED strips ("spokes"), one per
// clock-hour position, index 0 at 12 o'clock going clockwise. Within a spoke,
// LED 0 (data-in) is outermost and LED 4 (data-out) is innermost -- deduced
// from readme.md ("the last LED being the innermost at the 11 o'clock
// position", plus the data-in/data-out chaining description).
#define LEDS_PER_SPOKE 5
#define OUTER_RADIUS   105
#define INNER_RADIUS   25
#define DOT_RADIUS     4

CrowPanelRoundDisplay::LGFX::LGFX() {
  {
    auto cfg = bus.config();
    cfg.spi_host = SPI2_HOST;
    cfg.spi_mode = 0;
    cfg.freq_write = 80000000;
    cfg.freq_read = 20000000;
    cfg.spi_3wire = true;
    cfg.use_lock = true;
    cfg.dma_channel = SPI_DMA_CH_AUTO;
    cfg.pin_sclk = 6;
    cfg.pin_mosi = 7;
    cfg.pin_miso = -1;
    cfg.pin_dc = 2;
    bus.config(cfg);
    panel.setBus(&bus);
  }
  {
    auto cfg = panel.config();
    cfg.pin_cs = 10;
    cfg.pin_rst = -1;   // reset is via the IO expander, not a direct GPIO
    cfg.pin_busy = -1;
    cfg.memory_width = 240;
    cfg.memory_height = 240;
    cfg.panel_width = 240;
    cfg.panel_height = 240;
    cfg.offset_x = 0;
    cfg.offset_y = 0;
    cfg.offset_rotation = 0;
    cfg.dummy_read_pixel = 8;
    cfg.dummy_read_bits = 1;
    cfg.readable = false;
    cfg.invert = true;
    cfg.rgb_order = false;
    cfg.dlen_16bit = false;
    cfg.bus_shared = false;
    panel.config(cfg);
  }
  setPanel(&panel);
}

CrowPanelRoundDisplay::CrowPanelRoundDisplay(int numLeds_)
: numLeds(numLeds_)
{
  pending = new uint32_t[numLeds];
  lastShown = new uint32_t[numLeds];
  for (int i=0; i<numLeds; i++) {
    pending[i] = 0;
    // Force the first show() to actually draw every dot, even the ones that
    // are still black -- lastShown must not start out equal to pending.
    lastShown[i] = 0xffffffff;
  }
}

void CrowPanelRoundDisplay::begin() {
  Wire.begin(I2C_SDA, I2C_SCL);
  expander.begin();
  expander.setPin(EXPANDER_PIN_TOUCH_RESET, true);
  expander.setPin(EXPANDER_PIN_LCD_RESET, true);

  tft.init();
  tft.initDMA();
  tft.startWrite();
  tft.fillScreen(TFT_BLACK);
  tft.endWrite();

  // Backlight on last, mirroring the factory firmware's own sequencing, so
  // nothing garbled is ever visible on power-up.
  expander.setPin(EXPANDER_PIN_BACKLIGHT, true);
}

void CrowPanelRoundDisplay::clear() {
  tft.startWrite();
  tft.fillScreen(TFT_BLACK);
  tft.endWrite();
  for (int i=0; i<numLeds; i++) {
    pending[i] = 0;
    lastShown[i] = 0;
  }
}

void CrowPanelRoundDisplay::setPixel(int index, uint32_t rgb) {
  pending[index] = rgb;
}

uint32_t CrowPanelRoundDisplay::getPixel(int index) {
  return pending[index];
}

void CrowPanelRoundDisplay::show() {
  tft.startWrite();
  for (int i=0; i<numLeds; i++) {
    if (pending[i] != lastShown[i]) {
      int x, y;
      dotPosition(i, x, y);
      uint8_t r = (pending[i] >> 16) & 0xff;
      uint8_t g = (pending[i] >> 8) & 0xff;
      uint8_t b = pending[i] & 0xff;
      tft.fillCircle(x, y, DOT_RADIUS, tft.color565(r, g, b));
      lastShown[i] = pending[i];
    }
  }
  tft.endWrite();
}

void CrowPanelRoundDisplay::dotPosition(int index, int &x, int &y) {
  int spoke = index / LEDS_PER_SPOKE;
  int posInSpoke = index % LEDS_PER_SPOKE;
  int numSpokes = numLeds / LEDS_PER_SPOKE;
  float angle = 2*PI*spoke/numSpokes;  // spoke 0 = 12 o'clock, increasing clockwise
  float radius = OUTER_RADIUS - posInSpoke * ((OUTER_RADIUS - INNER_RADIUS) / (float)(LEDS_PER_SPOKE - 1));
  x = 120 + (int)(radius*sin(angle));
  y = 120 - (int)(radius*cos(angle));
}
#endif // WITH_ROUNDLCD_DISPLAY
