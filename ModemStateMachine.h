#ifndef MODEM_STATE_MACHINE_H
#define MODEM_STATE_MACHINE_H

#include <Arduino.h>
#include "ModemSteps.h"

class ModemStateMachine {
public:
  // Wait time_to_wait ms, then transition to next_step.
  void waitFixed(Modem_steps next_step, uint16_t time_to_wait);

  // Wait for `expected` to appear in a received line, or for `timeout` ms.
  // On match: transition to step_when_success. On timeout: transition to
  // step_when_timeout.
  void waitForResponse(const char* expected,
                       uint16_t timeout,
                       Modem_steps step_when_success,
                       Modem_steps step_when_timeout);

  // True when the current wait is satisfied; updates current_step accordingly.
  bool shouldExecute();

  Modem_steps current() const { return current_step; }

  // Force a transition (used for OFF/GO_SLEEP/SLEEP and external wake).
  void setCurrent(Modem_steps s);

  // Feed a received line; matches against the active expected_response.
  void onLineReceived(const char* line);

private:
  enum WaitMode { WAIT_TIMED, WAIT_RESPONSE };

  Modem_steps   current_step               = OFF;
  Modem_steps   step_when_success          = OFF;
  Modem_steps   step_when_timeout          = OFF;
  WaitMode      wait_mode                  = WAIT_TIMED;
  const char*   expected_response          = nullptr;
  bool          expected_response_received = false;
  unsigned long last_step_time             = 0;
  unsigned long timeout_ms                 = 0;
};

#endif
