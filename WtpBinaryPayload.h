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
  void setSampleCount(uint16_t count);       // 1..200, or up to 400 in whole minutes (multiple of 20)
  void setVoltage(float voltage);            // V, encoded as centivolts
  void setRSSI(int8_t rssi);                 // dBm, range -128..127
  void setPressure(float pressure_hpa);      // optional, hPa, encoded as deci-hPa
  void setTemperature(float temp_c);         // optional, °C, encoded as deci-°C (int16)
  void setMeta(const char* meta);            // optional, ASCII

  unsigned int calculatePayloadLength();
  void sendPayload(Print* modem, Print* debug = NULL);

  // Running modes carried by the server reply (WTP-bin.md, "Reply")
  static const uint8_t REPLY_SIZE = 22;
  static const uint8_t REPLY_MODE_AUTO = 0x01;
  static const uint8_t REPLY_MODE_INTERVAL = 0x02;      // value: seconds, 60..1200, multiple of 60
  static const uint8_t REPLY_MODE_HIBERNATE = 0x03;

  // Checks a reply to the last payload sent (size, version, tag over that
  // payload's timestamp). Fills mode/value and returns true when genuine.
  bool parseReply(const uint8_t* buf, uint8_t len, uint8_t* mode, int32_t* value);

private:
  static const uint8_t WTP_BIN_VERSION = 0x01;
  static const uint16_t VOLTAGE_UNKNOWN = 0xFFFF;
  static const int8_t RSSI_UNKNOWN = 0x7F;
  static const uint8_t SAMPLES_PER_MINUTE = 20;     // 3 s spacing
  static const uint8_t MAX_COUNT_IN_SAMPLES = 200;  // sample_count range without FLAG_COUNT_MINUTES
  static const uint8_t MAX_COUNT_IN_MINUTES = 20;   // sample_count range with FLAG_COUNT_MINUTES
  static const uint16_t MAX_SAMPLES_PER_PAYLOAD =
      (uint16_t)MAX_COUNT_IN_MINUTES * SAMPLES_PER_MINUTE;  // 400
  static const uint8_t FLAG_META = 0x01;
  static const uint8_t FLAG_PRESSURE = 0x02;
  static const uint8_t FLAG_TEMPERATURE = 0x04;
  static const uint8_t FLAG_COUNT_MINUTES = 0x08;   // sample_count counts minutes
  static const uint8_t MAX_META_LEN = 255;
  static const uint8_t HMAC_TAG_BYTES = 16;
  static const uint8_t REPLY_VERSION = 0x01;
  static const uint8_t REPLY_SIGNED_BYTES = 6;  // version, mode, value

  WN_Core* _anemometer = NULL;
  uint32_t _timestamp = 0;
  uint16_t _sample_count = 0;
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
