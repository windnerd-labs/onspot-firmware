#pragma once
#include <Arduino.h>

struct PowerConfig {
  float min_voltage;  // lower bound of this tier (inclusive)
  uint8_t period_mn;  // upload period in minutes
};

class PowerManager {
public:
  // pin: analog pin connected to the voltage divider.
  // configs: const array sorted descending by min_voltage (highest tier first).
  // hysteresis_v: voltage buffer applied on both sides of each threshold.
  PowerManager(uint8_t pin);

  void loop();                  // call every loop tick: samples voltage once per second
  void evaluate();              // lock in the active tier from current voltage
  float getVoltage() const;     // filtered voltage (V)
  uint8_t getPeriod() const;    // upload period (minutes): operator interval if set, else current tier
  bool shouldPowerOff() const;  // true -> AT+CPOF between uploads; false -> AT+CSCLK=2
  // Upload interval set by the station operator (WTP-bin reply), replacing the
  // battery tier period. 0 = auto, the tier decides.
  void setIntervalOverride(uint8_t period_mn);
  uint8_t getIntervalOverride() const;
  bool shouldPowerOn() const;   // false when voltage is below all tiers
  bool shouldHibernate() const; // battery too low even to wait for a recharge awake

private:
  void sampleVoltage();  // sample ADC and update EMA
  uint8_t _active_idx = 0;  // we want tiers with shortest period after device is started 
  uint8_t _interval_override_mn = 0;
  uint8_t _pin = 0;
  float _voltage = 0.0f;
  unsigned long _last_sample_ms = 0;
  bool _seeded = false;
};
