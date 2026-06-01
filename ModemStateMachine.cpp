#include "ModemStateMachine.h"
#include <string.h>

void ModemStateMachine::waitFixed(Modem_steps next_step, uint16_t time_to_wait) {
  expected_response = nullptr;
  expected_response_received = false;
  wait_mode = WAIT_TIMED;
  last_step_time = millis();
  timeout_ms = time_to_wait;
  current_step = next_step;
}

void ModemStateMachine::waitForResponse(const char* expected,
                                        uint16_t timeout,
                                        Modem_steps success_step,
                                        Modem_steps timeout_step) {
  expected_response = expected;
  expected_response_received = false;
  wait_mode = WAIT_RESPONSE;
  last_step_time = millis();
  timeout_ms = timeout;
  step_when_success = success_step;
  step_when_timeout = timeout_step;
}

void ModemStateMachine::setCurrent(Modem_steps s) {
  current_step = s;
  wait_mode = WAIT_TIMED;
  last_step_time = 0;
  timeout_ms = 0;
  expected_response = nullptr;
  expected_response_received = false;
}

bool ModemStateMachine::shouldExecute() {

  if (wait_mode == WAIT_TIMED) {
    return (millis() - last_step_time) > timeout_ms;
  }

  // WAIT_RESPONSE
  if (expected_response_received) {
    expected_response = nullptr;
    expected_response_received = false;
    current_step = step_when_success;
    wait_mode = WAIT_TIMED;
    last_step_time = 0;
    timeout_ms = 0;
    return true;
  }

  bool is_timeout_expired = (millis() - last_step_time) > timeout_ms;
  if (is_timeout_expired) {
    current_step = step_when_timeout;
    expected_response = nullptr;
    expected_response_received = false;
    wait_mode = WAIT_TIMED;
    last_step_time = 0;
    timeout_ms = 0;
    return true;
  }

  return false;
}

void ModemStateMachine::onLineReceived(const char* line) {
  if (expected_response != nullptr && strstr(line, expected_response)) {
    expected_response_received = true;
  }
}
