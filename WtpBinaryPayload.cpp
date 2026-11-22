/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "WtpBinaryPayload.h"

extern "C" {
#include "blake2.h"
}

// WTP credentials are provisioned per-device into the STM32 OTP area at the
// factory, so nothing is baked into the firmware image
#define OTP_BASE        0x1FFF7000UL
#define OTP_ID_ADDR     (OTP_BASE + 0x00)   // uint32_t  device id
#define OTP_KEY_ADDR    (OTP_BASE + 0x04)   // uint8_t[16] wtp key
#define WTP_KEY_LEN     16

// all ones in the 9-bit direction field, the binary counterpart of the -1
// "direction unknown" code used by the other WTP routes
#define WTP_DIR_UNKNOWN 0x1FF

// HMAC-Blake2s keyed with the OTP secret. Standard HMAC construction:
// H((K^opad) || H((K^ipad) || message)). Block size for Blake2s is 64 bytes;
// the 16-byte secret is zero-padded.
static void hmacPad(uint8_t pad[BLAKE2S_BLOCKBYTES], uint8_t fill) {
  const volatile uint8_t* secret_key = (const volatile uint8_t*)OTP_KEY_ADDR;
  memset(pad, fill, BLAKE2S_BLOCKBYTES);
  for (size_t i = 0; i < WTP_KEY_LEN; i++) {
    pad[i] ^= secret_key[i];
  }
}

// Starts the inner hash; feed the message with blake2s_update()
static void hmacBegin(blake2s_state* inner) {
  uint8_t k_ipad[BLAKE2S_BLOCKBYTES];
  hmacPad(k_ipad, 0x36);
  blake2s_init(inner, BLAKE2S_OUTBYTES);
  blake2s_update(inner, k_ipad, BLAKE2S_BLOCKBYTES);
}

// Finalizes the inner hash, then the outer one into out (full 32 bytes)
static void hmacFinish(blake2s_state* inner, uint8_t out[BLAKE2S_OUTBYTES]) {
  uint8_t inner_hash[BLAKE2S_OUTBYTES];
  blake2s_final(inner, inner_hash, BLAKE2S_OUTBYTES);

  uint8_t k_opad[BLAKE2S_BLOCKBYTES];
  hmacPad(k_opad, 0x5C);
  blake2s_state outer;
  blake2s_init(&outer, BLAKE2S_OUTBYTES);
  blake2s_update(&outer, k_opad, BLAKE2S_BLOCKBYTES);
  blake2s_update(&outer, inner_hash, BLAKE2S_OUTBYTES);
  blake2s_final(&outer, out, BLAKE2S_OUTBYTES);
}

WtpBinaryPayload::WtpBinaryPayload() {
}

void WtpBinaryPayload::reset() {
  _voltage_cv = VOLTAGE_UNKNOWN;
  _rssi = RSSI_UNKNOWN;
  _meta = NULL;
  _has_meta = false;
  _has_pressure = false;
  _has_temperature = false;
  _sample_count = 0;
  _timestamp = 0;
}

void WtpBinaryPayload::setAnemometer(WN_Core* anemometer) {
  _anemometer = anemometer;
}

void WtpBinaryPayload::setTimestamp(uint32_t unix_seconds) {
  _timestamp = unix_seconds;
}

void WtpBinaryPayload::setSampleCount(uint16_t count) {
  if (count < 1) count = 1;
  if (count > MAX_SAMPLES_PER_PAYLOAD) count = MAX_SAMPLES_PER_PAYLOAD;
  // Above MAX_COUNT_IN_SAMPLES the header byte counts whole minutes (flag bit 3),
  // so the count must be a multiple of SAMPLES_PER_MINUTE; drop the oldest partial minute
  if (count > MAX_COUNT_IN_SAMPLES) count -= count % SAMPLES_PER_MINUTE;
  _sample_count = count;
}

void WtpBinaryPayload::setVoltage(float voltage) {
  if (voltage < 0) {
    _voltage_cv = VOLTAGE_UNKNOWN;
    return;
  }
  long cv = (long)(voltage * 100.0f + 0.5f);
  if (cv > 65534) cv = 65534;  // 0xFFFF reserved for "unknown"
  _voltage_cv = (uint16_t)cv;
}

void WtpBinaryPayload::setRSSI(int8_t rssi) {
  _rssi = rssi;
}

void WtpBinaryPayload::setPressure(float pressure_hpa) {
  if (pressure_hpa < 0) {
    _has_pressure = false;
    return;
  }
  long dh = (long)(pressure_hpa * 10.0f + 0.5f);
  if (dh > 65535) dh = 65535;
  _pressure_dhpa = (uint16_t)dh;
  _has_pressure = true;
}

void WtpBinaryPayload::setTemperature(float temp_c) {
  float scaled = temp_c * 10.0f;
  scaled += (scaled >= 0) ? 0.5f : -0.5f;
  long dc = (long)scaled;
  if (dc > 32767) dc = 32767;
  if (dc < -32768) dc = -32768;
  _temperature_dc = (int16_t)dc;
  _has_temperature = true;
}

void WtpBinaryPayload::setMeta(const char* meta) {
  _meta = meta;
  _has_meta = (meta != NULL && meta[0] != '\0');
}

unsigned int WtpBinaryPayload::calculatePayloadLength() {
  unsigned int len = 14u + 3u * (unsigned)_sample_count + HMAC_TAG_BYTES;
  if (_has_pressure) len += 2u;
  if (_has_temperature) len += 2u;
  if (_has_meta) {
    size_t meta_len = strlen(_meta);
    if (meta_len > MAX_META_LEN) meta_len = MAX_META_LEN;
    len += 1u + (unsigned)meta_len;
  }
  return len;
}

// Pack one wind sample into 3 big-endian bytes:
//   bits 23-14 : speed (round(m/s * 10), 0..990)
//   bits 13-5  : direction (0..359, or WTP_DIR_UNKNOWN)
//   bits 4-0   : reserved (0)
void WtpBinaryPayload::packSample(const wn_instant_wind_sample_t& sample, uint8_t out[3]) {
  int speed = (int)(sample.speed * 10.0f + 0.5f);
  if (speed < 0) speed = 0;
  if (speed > 990) speed = 990;

  int dir = (int)sample.dir;
  if (dir == WN_DIR_UNKNOWN) dir = WTP_DIR_UNKNOWN;
  else if (dir < 0) dir = 0;
  else if (dir > 359) dir = 359;

  uint32_t packed = ((uint32_t)speed << 14) | ((uint32_t)dir << 5);
  out[0] = (uint8_t)((packed >> 16) & 0xFF);
  out[1] = (uint8_t)((packed >> 8) & 0xFF);
  out[2] = (uint8_t)(packed & 0xFF);
}

void WtpBinaryPayload::sendPayload(Print* modem, Print* debug) {

  // Credentials read straight from OTP at signing time
  const uint32_t device_id = *(volatile const uint32_t*)OTP_ID_ADDR;

  blake2s_state inner;
  hmacBegin(&inner);

  // --- Header (14 bytes, big-endian) --------------------------------------
  uint8_t header[14];
  uint8_t flags = 0;
  bool count_in_minutes = _sample_count > MAX_COUNT_IN_SAMPLES;
  if (_has_meta) flags |= FLAG_META;
  if (_has_pressure) flags |= FLAG_PRESSURE;
  if (_has_temperature) flags |= FLAG_TEMPERATURE;
  if (count_in_minutes) flags |= FLAG_COUNT_MINUTES;
  header[0] = WTP_BIN_VERSION;
  header[1] = flags;
  header[2] = (uint8_t)((device_id >> 24) & 0xFF);
  header[3] = (uint8_t)((device_id >> 16) & 0xFF);
  header[4] = (uint8_t)((device_id >> 8) & 0xFF);
  header[5] = (uint8_t)(device_id & 0xFF);
  header[6] = (uint8_t)((_timestamp >> 24) & 0xFF);
  header[7] = (uint8_t)((_timestamp >> 16) & 0xFF);
  header[8] = (uint8_t)((_timestamp >> 8) & 0xFF);
  header[9] = (uint8_t)(_timestamp & 0xFF);
  header[10] = (uint8_t)(count_in_minutes ? _sample_count / SAMPLES_PER_MINUTE : _sample_count);
  header[11] = (uint8_t)((_voltage_cv >> 8) & 0xFF);
  header[12] = (uint8_t)(_voltage_cv & 0xFF);
  header[13] = (uint8_t)_rssi;

  modem->write(header, sizeof(header));
  blake2s_update(&inner, header, sizeof(header));

  // --- Samples (3 bytes each, most-recent-first via getSampleIndexedFromLast)
  for (uint16_t i = 0; i < _sample_count; i++) {
    wn_instant_wind_sample_t sample = _anemometer->getSampleIndexedFromLast(i);
    uint8_t s[3];
    packSample(sample, s);
    modem->write(s, sizeof(s));
    blake2s_update(&inner, s, sizeof(s));
  }

  // --- Optional extras block ----------------------------------------------
  // Fields appear in flag-bit order (lowest bit first); each is fixed-size.
  if (_has_pressure) {
    uint8_t p[2];
    p[0] = (uint8_t)((_pressure_dhpa >> 8) & 0xFF);
    p[1] = (uint8_t)(_pressure_dhpa & 0xFF);
    modem->write(p, sizeof(p));
    blake2s_update(&inner, p, sizeof(p));
  }
  if (_has_temperature) {
    uint16_t u = (uint16_t)_temperature_dc;  // two's complement bit pattern
    uint8_t t[2];
    t[0] = (uint8_t)((u >> 8) & 0xFF);
    t[1] = (uint8_t)(u & 0xFF);
    modem->write(t, sizeof(t));
    blake2s_update(&inner, t, sizeof(t));
  }

  // --- Optional meta block ------------------------------------------------
  if (_has_meta) {
    size_t meta_len = strlen(_meta);
    if (meta_len > MAX_META_LEN) meta_len = MAX_META_LEN;
    uint8_t meta_hdr = (uint8_t)meta_len;
    modem->write(&meta_hdr, 1);
    modem->write((const uint8_t*)_meta, meta_len);
    blake2s_update(&inner, &meta_hdr, 1);
    blake2s_update(&inner, (const uint8_t*)_meta, meta_len);
  }

  // --- Finalize, then send truncated tag -----------------------------------
  uint8_t outer_hash[BLAKE2S_OUTBYTES];
  hmacFinish(&inner, outer_hash);

  modem->write(outer_hash, HMAC_TAG_BYTES);

  if (debug) {
    debug->print("WTP-bin payload sent, ");
    debug->print(calculatePayloadLength());
    debug->println(" bytes");
  }
}

// Reply frame (WTP-bin.md, "Reply"): version | mode | value (int32 BE) | tag,
// tag = HMAC(timestamp of the request || frame[0:6])[0:16]. _timestamp still
// holds the request's, reset() only runs when the next payload is built.
bool WtpBinaryPayload::parseReply(const uint8_t* buf, uint8_t len, uint8_t* mode, int32_t* value) {
  if (len != REPLY_SIZE || buf[0] != REPLY_VERSION) return false;

  uint8_t ts[4];
  ts[0] = (uint8_t)((_timestamp >> 24) & 0xFF);
  ts[1] = (uint8_t)((_timestamp >> 16) & 0xFF);
  ts[2] = (uint8_t)((_timestamp >> 8) & 0xFF);
  ts[3] = (uint8_t)(_timestamp & 0xFF);

  blake2s_state inner;
  hmacBegin(&inner);
  blake2s_update(&inner, ts, sizeof(ts));
  blake2s_update(&inner, buf, REPLY_SIGNED_BYTES);
  uint8_t tag[BLAKE2S_OUTBYTES];
  hmacFinish(&inner, tag);

  // constant-time compare
  uint8_t diff = 0;
  for (uint8_t i = 0; i < HMAC_TAG_BYTES; i++) {
    diff |= tag[i] ^ buf[REPLY_SIGNED_BYTES + i];
  }
  if (diff != 0) return false;

  *mode = buf[1];
  *value = (int32_t)(((uint32_t)buf[2] << 24) | ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | (uint32_t)buf[5]);
  return true;
}
