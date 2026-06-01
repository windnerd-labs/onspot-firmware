#pragma once
#include <Arduino.h>

struct PowerConfig {
  float min_voltage;  // lower bound of this tier (inclusive)
  uint8_t period_mn;  // upload period in minutes
  bool power_off;     // true -> AT+CPWROFF between uploads; false -> AT+CSCLK=2
};

class PowerManager {
public:
  // pin: analog pin connected to the voltage divider.
  // configs: const array sorted descending by min_voltage (highest tier first).
  // hysteresis_v: voltage buffer applied on both sides of each threshold.
  PowerManager(uint8_t pin);

  void loop();                  // call every loop tick: samples voltage
  void evaluate();              // lock in the active tier from current voltage
  float getVoltage() const;     // filtered voltage (V)
  uint8_t getPeriod() const;    // minutes for current tier (0 if no tier active)
  bool shouldPowerOff() const;  // true -> AT+CPWROFF; false -> AT+CSCLK=2
  bool shouldPowerOn() const;   // false when voltage is below all tiers

private:
  void sampleVoltage();  // sample ADC and update EMA
  uint8_t _active_idx = 0;  // we want tiers with shortest period after device is started 
  uint8_t _pin = 0;
  float _voltage = 0.0f;
  bool _seeded = false;
};
