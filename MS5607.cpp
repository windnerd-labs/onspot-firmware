/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "MS5607.h"

MS5607::MS5607(WN_I2C* i2c, uint8_t address)
  : _i2c(i2c), _address(address) {}

bool MS5607::begin() {
  _present = false;

  if (_i2c->writeCommand(_address, CMD_RESET) != WN_I2C_OK) return false;
  delay(3);  // datasheet: reload of calibration takes 2.8 ms max

  if (!readPROM()) return false;
  if (crc4(_prom) != (_prom[7] & 0x000F)) return false;

  _present = true;
  return true;
}

bool MS5607::readPROM() {
  for (uint8_t i = 0; i < 8; i++) {
    uint8_t buf[2];
    if (_i2c->readRegister(_address, CMD_PROM_READ_BASE + (i << 1), buf, 2) != WN_I2C_OK) return false;
    _prom[i] = ((uint16_t)buf[0] << 8) | buf[1];
  }
  return true;
}

bool MS5607::readADC(uint32_t* value) {
  uint8_t buf[3];
  if (_i2c->readRegister(_address, CMD_ADC_READ, buf, 3) != WN_I2C_OK) return false;
  *value = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
  return true;
}

bool MS5607::read() {
  if (!_present) return false;

  uint32_t D1 = 0, D2 = 0;

  if (_i2c->writeCommand(_address, CMD_CONVERT_D1_OSR4096) != WN_I2C_OK) return false;
  delay(CONVERSION_DELAY_MS);
  if (!readADC(&D1)) return false;

  if (_i2c->writeCommand(_address, CMD_CONVERT_D2_OSR4096) != WN_I2C_OK) return false;
  delay(CONVERSION_DELAY_MS);
  if (!readADC(&D2)) return false;

  // First-order temperature/pressure compensation (datasheet §"PRESSURE AND
  // TEMPERATURE CALCULATION"). All intermediates kept in 64-bit signed math.
  int32_t dT = (int32_t)D2 - ((int32_t)_prom[5] << 8);
  int32_t TEMP = 2000 + (int32_t)(((int64_t)dT * _prom[6]) >> 23);

  int64_t OFF = ((int64_t)_prom[2] << 17) + (((int64_t)_prom[4] * dT) >> 6);
  int64_t SENS = ((int64_t)_prom[1] << 16) + (((int64_t)_prom[3] * dT) >> 7);

  // Second-order compensation for low temperatures.
  if (TEMP < 2000) {
    int32_t t2 = (int32_t)(((int64_t)dT * dT) >> 31);
    int64_t off2 = (61 * (int64_t)(TEMP - 2000) * (TEMP - 2000)) >> 4;
    int64_t sens2 = 2 * (int64_t)(TEMP - 2000) * (TEMP - 2000);
    if (TEMP < -1500) {
      off2 += 15 * (int64_t)(TEMP + 1500) * (TEMP + 1500);
      sens2 += 8 * (int64_t)(TEMP + 1500) * (TEMP + 1500);
    }
    TEMP -= t2;
    OFF -= off2;
    SENS -= sens2;
  }

  int32_t P = (int32_t)(((((int64_t)D1 * SENS) >> 21) - OFF) >> 15);

  _temperature_c = TEMP / 100.0f;  // datasheet returns 0.01 °C
  _pressure_hpa = P / 100.0f;      // datasheet returns 0.01 mbar = 0.01 hPa
  return true;
}

// CRC-4 over the 16 nibbles of the PROM, as specified in MS5607 application
// note AN520. The CRC field itself (low nibble of word 7) is masked to zero
// during the calculation.
uint8_t MS5607::crc4(uint16_t prom[8]) {
  uint16_t n_rem = 0;
  uint16_t crc_read = prom[7];
  prom[7] = prom[7] & 0xFF00;

  for (uint8_t cnt = 0; cnt < 16; cnt++) {
    uint16_t word = prom[cnt >> 1];
    uint8_t byte = (cnt & 1) ? (word & 0x00FF) : (word >> 8);
    n_rem ^= byte;
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (n_rem & 0x8000) n_rem = (n_rem << 1) ^ 0x3000;
      else n_rem = (n_rem << 1);
    }
  }
  prom[7] = crc_read;
  return (uint8_t)((n_rem >> 12) & 0x000F);
}
