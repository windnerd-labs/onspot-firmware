/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include "Arduino.h"
#include "Windnerd_Core.h"

// WTP-bin (binary) variant of the WindNerd Transfer Protocol — see WTP-bin.md.
// Carries only high-frequency wind samples plus voltage / RSSI / optional meta,
// signed with HMAC-Blake2s-128. Designed for LoRa, NB-IoT, Sigfox, SMS bursts.
class WtpBinaryPayload {

public:
  WtpBinaryPayload();

  void reset();
  void setAnemometer(WN_Core* anemometer);
  void setTimestamp(uint32_t unix_seconds);  // Unix epoch of most recent sample
  void setSampleCount(uint8_t count);        // 1..200
  void setVoltage(float voltage);            // V, encoded as centivolts
  void setRSSI(int8_t rssi);                 // dBm, range -128..127
  void setPressure(float pressure_hpa);      // optional, hPa, encoded as deci-hPa
  void setTemperature(float temp_c);         // optional, °C, encoded as deci-°C (int16)
  void setMeta(const char* meta);            // optional, ASCII

  unsigned int calculatePayloadLength();
  void sendPayload(Print* modem, Print* debug = NULL);

private:
  static const uint8_t WTP_BIN_VERSION = 0x01;
  static const uint16_t VOLTAGE_UNKNOWN = 0xFFFF;
  static const int8_t RSSI_UNKNOWN = 0x7F;
  static const uint8_t MAX_SAMPLE_COUNT = 200;
  static const uint8_t MAX_META_LEN = 255;
  static const uint8_t HMAC_TAG_BYTES = 16;

  WN_Core* _anemometer = NULL;
  uint32_t _timestamp = 0;
  uint8_t _sample_count = 0;
  uint16_t _voltage_cv = VOLTAGE_UNKNOWN;
  int8_t _rssi = RSSI_UNKNOWN;
  uint16_t _pressure_dhpa = 0;
  int16_t _temperature_dc = 0;
  const char* _meta = NULL;
  bool _has_pressure = false;
  bool _has_temperature = false;
  bool _has_meta = false;

  static void packSample(const wn_instant_wind_sample_t& sample, uint8_t out[3]);
};
