#ifndef _PI4IOE5V6408_H_
#define _PI4IOE5V6408_H_
#include <Arduino.h>

// Driver for the PI4IOE5V6408 I2C GPIO expander on the Elecrow CrowPanel round
// display, which gates the LCD reset, touch reset and backlight-enable lines
// (see doc/board-elecrow-crowpanel-1.28.md for the pin table). Register
// protocol reverse-engineered from the vendor factory firmware's
// init_IO_extender()/set_pin_io() (Esp32_Watch_Demo.ino). Only P0-P4 are ever
// configured -- P5-P7 are unused on this board.
class PI4IOE5V6408 {
public:
  PI4IOE5V6408(uint8_t i2cAddr=0x43) : addr(i2cAddr), outputState(0) {}
  void begin();  // configure P0-P4 as driven outputs, all low
  void setPin(uint8_t pin, bool value);
private:
  void writeReg(uint8_t reg, uint8_t value);
  uint8_t addr;
  uint8_t outputState;
};

#endif
