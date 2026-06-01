/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <Arduino.h>
#include "A7670Manager.h"  // for ConnectivityState

// Drives a single status LED wired open-drain to GND (anode -> VCC via
// resistor, cathode -> MCU pin). Writing LOW sinks current and lights the LED.
//
// The indicator emits a burst of N short flashes every 2 seconds. N encodes
// the worst current condition:
//   1 — network connected, pressure sensor present
//   2 — network connected, no pressure sensor
//   3 — SIM ready, network not connected
//   4 — SIM PIN / PUK required
//   5 — SIM removed
//   6 — modem state unknown
//
// One condition overrides the burst entirely: when the battery is below every
// power tier the modem can't be started, so the LED falls back to a slow
// long on/off blink that no burst pattern can be mistaken for.
class StatusIndicator {
public:
  StatusIndicator(uint8_t pin = PA1);

  void begin();
  void setPressureSensorPresent(bool present);
  void setConnectivityState(ConnectivityState state);
  void setCanPowerOn(bool can);  // false when voltage is below all tiers
  void loop();

private:
  uint8_t _pin;
  bool _pressure_sensor_present = false;
  bool _can_power_on = true;
  ConnectivityState _state = ConnectivityState::UNKNOWN;

  // Pattern playback. Flash count is frozen at the start of each 2s cycle so
  // a state change mid-burst doesn't produce a half-pattern.
  unsigned long _cycle_start_ms = 0;
  uint8_t _frozen_flash_count = 0;
  bool _led_on = false;

  uint8_t determineFlashCount() const;
  void setLed(bool on);
};
