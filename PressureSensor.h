/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <Arduino.h>

// Abstract barometric pressure / temperature sensor.
// Concrete drivers (e.g. MS5607, BMP580) inherit from this so the rest of
// the firmware can talk to whichever chip is wired to the sensor connector
// without caring about its specifics.
class PressureSensor {
public:
  virtual ~PressureSensor() = default;

  // Probes the device. Returns true if the chip responded and is ready.
  virtual bool begin() = 0;

  // Triggers a fresh sample and updates cached pressure/temperature.
  // Returns false if the sensor did not respond or begin() never succeeded.
  virtual bool read() = 0;

  virtual float getPressure() const = 0;     // hPa (mbar)
  virtual float getTemperature() const = 0;  // °C
};

// Walks a list of candidate sensors and returns the first one whose begin()
// succeeds, or nullptr if none answered. Candidates are tried in order.
inline PressureSensor* probePressureSensor(PressureSensor* const* candidates, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (candidates[i]->begin()) return candidates[i];
  }
  return nullptr;
}
