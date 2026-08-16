#include "PI4IOE5V6408.h"
#include <Wire.h>

#define REG_IO_DIRECTION 0x03
#define REG_OUTPUT_STATE 0x05
#define REG_OUTPUT_HIZ   0x07
#define PINS_0_TO_4      0x1f

void PI4IOE5V6408::begin() {
  writeReg(REG_IO_DIRECTION, PINS_0_TO_4);            // P0-P4 as outputs
  writeReg(REG_OUTPUT_HIZ, (uint8_t)~PINS_0_TO_4);    // P0-P4 driven, not Hi-Z
  outputState = 0;
  writeReg(REG_OUTPUT_STATE, outputState);
}

void PI4IOE5V6408::setPin(uint8_t pin, bool value) {
  if (value) outputState |= (1 << pin);
  else outputState &= ~(1 << pin);
  writeReg(REG_OUTPUT_STATE, outputState);
}

void PI4IOE5V6408::writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}
