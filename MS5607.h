/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <Arduino.h>
#include "Windnerd_I2C.h"
#include "PressureSensor.h"

// Driver for the MS5607-02BA03 barometric pressure / temperature sensor
// over I2C. Datasheet: MS5607-02BA03 rev B (TE Connectivity).
//
// Uses OSR=4096 for both pressure (D1) and temperature (D2); each conversion
// needs ~9.04 ms typ / 10 ms max, so a full read() blocks for ~22 ms.
class MS5607 : public PressureSensor {
public:
  // address: 7-bit I2C address. 0x76 with CSB pin high, 0x77 with CSB low.
  MS5607(WN_I2C* i2c, uint8_t address = 0x77);

  bool begin() override;
  bool read() override;

  float getPressure() const override {
    return _pressure_hpa;
  }  // hPa (mbar)
  float getTemperature() const override {
    return _temperature_c;
  }  // °C

private:
  static const uint8_t CMD_RESET = 0x1E;
  static const uint8_t CMD_CONVERT_D1_OSR4096 = 0x48;
  static const uint8_t CMD_CONVERT_D2_OSR4096 = 0x58;
  static const uint8_t CMD_ADC_READ = 0x00;
  static const uint8_t CMD_PROM_READ_BASE = 0xA0;
  static const uint8_t CONVERSION_DELAY_MS = 10;

  WN_I2C* _i2c;
  uint8_t _address;
  uint16_t _prom[8] = { 0 };  // [0]=factory, [1..6]=calibration, [7]=CRC
  bool _present = false;
  float _pressure_hpa = 0.0f;
  float _temperature_c = 0.0f;

  bool readPROM();
  bool readADC(uint32_t* value);
  static uint8_t crc4(uint16_t prom[8]);
};
