#include "PowerManager.h"
#include "Logging.h"



#define POWER_CONFIGS_LENGTH 4
// Voltage tiers sorted descending by min_voltage (highest tier first).
const PowerConfig power_configs[POWER_CONFIGS_LENGTH] = {
  { 4.05f, 1, false },
  { 3.9f, 2, false },
  { 3.75f, 4, false },
  { 3.65f, 6, true },
};


#define HYSTERESIS 0.04f


PowerManager::PowerManager(uint8_t pin) {
  _pin = pin;
  _voltage = 0.0f;
  _seeded = false;
  sampleVoltage();
}

void PowerManager::loop() {
  sampleVoltage();
}

void PowerManager::sampleVoltage() {
  // Read and scale ADC: 2V reference, 10-bit, 3x voltage divider.
  float v = analogRead(_pin) * 2.0f / 1024 * 3.0f;

  // Exponential moving average (alpha = 0.1). Seed with first reading.
  if (!_seeded) {
    _voltage = v;
    _seeded = true;
  } else {
    _voltage += (v - _voltage) * 0.02f;
  }
}

void PowerManager::evaluate() {
  if (_voltage >= power_configs[_active_idx].min_voltage - HYSTERESIS) {
    // Still in current tier; check if we can upgrade to a higher one.
    for (uint8_t i = 0; i < _active_idx; i++) {
      if (_voltage >= power_configs[i].min_voltage + HYSTERESIS) {
        _active_idx = i;
        break;
      }
    }
  } else {
    while (_active_idx < POWER_CONFIGS_LENGTH - 1) {
      _active_idx++;
      if (_voltage >= power_configs[_active_idx].min_voltage)
        break;
    }
  }
  // millivolts as integer to stay independent of float printf support
  LOG(LOG_I, "PWR", "mv=%d tier=%u", (int)(_voltage * 1000.0f + 0.5f), _active_idx);
}

float PowerManager::getVoltage() const {
  return _voltage;
}

uint8_t PowerManager::getPeriod() const {
  return power_configs[_active_idx].period_mn;
}

bool PowerManager::shouldPowerOff() const {
  return power_configs[_active_idx].power_off;
}

bool PowerManager::shouldPowerOn() const {
  for (uint8_t i = 0; i < POWER_CONFIGS_LENGTH; i++) {
    if (_voltage >= power_configs[i].min_voltage) return true;
  }
  return false;
}
