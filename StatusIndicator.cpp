/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StatusIndicator.h"

// Each flash slot is ON_MS lit + OFF_MS dark. N slots run back-to-back at the
// start of every PERIOD_MS window; the LED is dark for the remainder.
// 7 flashes (worst case) take 7 * 400 = 2800 ms, leaving 200 ms of idle gap.
static constexpr unsigned long ON_MS = 200;
static constexpr unsigned long OFF_MS = 200;
static constexpr unsigned long PERIOD_MS = 3000;

// Low-battery fallback: a slow 50% blink, half-period per LED state. The long
// ON time never occurs in the fast burst patterns, so this can't be mistaken
// for a flash count.
static constexpr unsigned long SLOW_HALF_MS = 2000;

StatusIndicator::StatusIndicator(uint8_t pin)
  : _pin(pin) {}

void StatusIndicator::begin() {
  pinMode(_pin, OUTPUT_OPEN_DRAIN);
  setLed(false);
  _cycle_start_ms = millis();
  _frozen_flash_count = determineFlashCount();
}

void StatusIndicator::setPressureSensorPresent(bool present) {
  _pressure_sensor_present = present;
}

void StatusIndicator::setConnectivityState(ConnectivityState state) {
  _state = state;
}

void StatusIndicator::setCanPowerOn(bool can) {
  _can_power_on = can;
}

uint8_t StatusIndicator::determineFlashCount() const {
  switch (_state) {
    case ConnectivityState::UNKNOWN: return 6;
    case ConnectivityState::SIM_REMOVED: return 5;
    case ConnectivityState::SIM_PIN_REQUIRED: return 4;
    case ConnectivityState::SIM_READY: return 3;
    case ConnectivityState::NETWORK_OK:
      return _pressure_sensor_present ? 1 : 2;
  }
  return 6;
}

void StatusIndicator::setLed(bool on) {
  // Open-drain to GND: LOW sinks current and lights the LED, HIGH is high-Z.
  digitalWrite(_pin, on ? LOW : HIGH);
  _led_on = on;
}

void StatusIndicator::loop() {
  unsigned long now = millis();

  if (!_can_power_on) {
    // Battery below every power tier: modem can't start. Override the burst
    // with a slow 50/50 blink by toggling every half-period.
    if (now - _cycle_start_ms >= SLOW_HALF_MS) {
      _cycle_start_ms = now;
      setLed(!_led_on);
    }
    return;
  }

  unsigned long elapsed = now - _cycle_start_ms;

  if (elapsed >= PERIOD_MS) {
    _cycle_start_ms = now;
    _frozen_flash_count = determineFlashCount();
    elapsed = 0;
  }

  const unsigned long burst_duration =
    (unsigned long)_frozen_flash_count * (ON_MS + OFF_MS);

  bool want_on = false;
  if (elapsed < burst_duration) {
    const unsigned long slot_pos = elapsed % (ON_MS + OFF_MS);
    want_on = slot_pos < ON_MS;
  }

  if (want_on != _led_on) {
    setLed(want_on);
  }
}
