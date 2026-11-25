#include "PowerManager.h"
#include "Logging.h"



// Voltage tiers sorted descending by min_voltage (highest tier first).
// period_mn must not exceed 20
const PowerConfig power_configs[] = {
  { 4.05f, 1 },
  { 3.95f, 2 },
  { 3.90f, 4 },
  { 3.80f, 6 },
  { 3.75f, 10 },
  { 3.65f, 20 },
};
#define POWER_CONFIGS_LENGTH (sizeof(power_configs) / sizeof(power_configs[0]))


#define HYSTERESIS 0.04f

// Voltage is sampled on a fixed period rather than at every loop wake
#define VOLTAGE_SAMPLE_PERIOD_MS 1000UL
#define VOLTAGE_EMA_ALPHA 0.2f

// Below this voltage the device hibernates (MCU stopped, modem off)
#define HIBERNATE_BELOW_V 3.55f

// Above this upload period the modem is powered off between uploads instead of
// sleeping: re-registering costs less than idling that long
#define POWER_OFF_ABOVE_MN 8


PowerManager::PowerManager(uint8_t pin) {
  _pin = pin;
  _voltage = 0.0f;
  _seeded = false;
  sampleVoltage();
}

void PowerManager::loop() {
  if (millis() - _last_sample_ms >= VOLTAGE_SAMPLE_PERIOD_MS) {
    sampleVoltage();
  }
}

void PowerManager::sampleVoltage() {
  // Read and scale ADC: 2V reference, 10-bit, 3x voltage divider.
  float v = analogRead(_pin) * 2.0f / 1024 * 3.0f;
  _last_sample_ms = millis();

  // Exponential moving average. Seed with first reading.
  if (!_seeded) {
    _voltage = v;
    _seeded = true;
  } else {
    _voltage += (v - _voltage) * VOLTAGE_EMA_ALPHA;
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
  if (_interval_override_mn) return _interval_override_mn;
  return power_configs[_active_idx].period_mn;
}

bool PowerManager::shouldPowerOff() const {
  return getPeriod() > POWER_OFF_ABOVE_MN;
}

void PowerManager::setIntervalOverride(uint8_t period_mn) {
  _interval_override_mn = period_mn;
}

uint8_t PowerManager::getIntervalOverride() const {
  return _interval_override_mn;
}

bool PowerManager::shouldPowerOn() const {
  for (uint8_t i = 0; i < POWER_CONFIGS_LENGTH; i++) {
    if (_voltage >= power_configs[i].min_voltage) return true;
  }
  return false;
}

bool PowerManager::shouldHibernate() const {
  return _voltage < HIBERNATE_BELOW_V;
}
