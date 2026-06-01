#include "Telemetry.h"
#include "stm32g0xx_hal.h"
#include <stdio.h>

// Decode the most diagnostically useful reset cause from the RCC reset flags,
// then clear them so the next boot reports fresh. IWDG/WWDG mean the firmware
// hung; PINRSTF is asserted on nearly every reset, so it is checked last.
static const char* resetCauseCode() {
  const char* code = "UNK";
  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) code = "IWDG";
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST)) code = "WWDG";
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST)) code = "LPWR";
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)) code = "SFT";
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PWRRST)) code = "POR";
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)) code = "PIN";
  __HAL_RCC_CLEAR_RESET_FLAGS();
  return code;
}

void Telemetry::captureResetCause() {
  reset_cause = resetCauseCode();
}

void Telemetry::setVersion(const char* v) {
  version = v;
}

void Telemetry::onCycleStart(bool a7670_powered) {
  a7670_powered_at_start = a7670_powered;
  cycle_start_ms = millis();
}

void Telemetry::onStep(Modem_steps step) {
  // Only protocol command steps (<= POST) can "fail"; ON_SUCCESS and beyond are
  // terminal bookkeeping states. Track the furthest one so onCycleResult() can
  // attribute a non-200 cycle to the step that actually broke.
  if (step <= POST) {
    last_protocol_step = step;
  }
}

void Telemetry::onSetHeaders() {
  set_headers_ms = millis();
  t_start_to_set_headers_ms = set_headers_ms - cycle_start_ms;
}

void Telemetry::onGoSleep() {
  if (set_headers_ms != 0) {
    t_set_headers_to_go_sleep_ms = millis() - set_headers_ms;
  }
}

void Telemetry::onCycleResult(int http_code) {
  last_http_code = http_code;
  if (http_code == 200) {
    consecutive_failures = 0;
  } else {
    consecutive_failures++;
    fail_step = last_protocol_step;
  }
}

static const char* stepName(Modem_steps s) {
  switch (s) {
    case STARTING: return "STARTING";
    case START_FROM_OFF: return "START_FROM_OFF";
    case SET_APN: return "SET_APN";
    case ENABLE_TIME: return "ENABLE_TIME";
    case REQUEST_TIME: return "REQUEST_TIME";
    case REQUEST_RSSI: return "REQUEST_RSSI";
    case TERMINATE_HTTP: return "TERMINATE_HTTP";
    case INIT: return "INIT";
    case INIT_RETRY: return "INIT_RETRY";
    case SET_URL: return "SET_URL";
    case SET_HEADERS: return "SET_HEADERS";
    case SET_APPLICATION_TYPE: return "SET_APP_TYPE";
    case SET_DATA: return "SET_DATA";
    case POST: return "POST";
    default: return "?";
  }
}

const char* Telemetry::buildMeta() {
  unsigned long uptime_s = millis() / 1000UL;
  int n = snprintf(meta_buffer, sizeof(meta_buffer),
                   "ver=%s,rst=%s,up=%lu,pwr=%u,sh=%lu,sg=%lu,cf=%u",
                   version,
                   reset_cause,
                   uptime_s,
                   (unsigned)(a7670_powered_at_start ? 1 : 0),
                   t_start_to_set_headers_ms,
                   t_set_headers_to_go_sleep_ms,
                   consecutive_failures);
  if (n > 0 && (size_t)n < sizeof(meta_buffer) && last_http_code != 0 && last_http_code != 200) {
    snprintf(meta_buffer + n, sizeof(meta_buffer) - n,
             ",fail=%d,fstep=%s", last_http_code, stepName(fail_step));
  }
  return meta_buffer;
}
