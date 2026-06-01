#pragma once
#include <Arduino.h>
#include "ModemSteps.h"

// Collects per-cycle diagnostics signaled from the upload state machine and
// renders them as a compact, human-readable string suitable for transmitting
// in the WTP payload meta field.
//
// Encoding (comma-separated key=value pairs, <=256 chars):
//   ver   = firmware version string (SOFT_VERSION)
//   rst   = reset cause of the last boot (IWDG/WWDG/LPWR/SFT/POR/PIN/UNK)
//   up    = device uptime in seconds
//   pwr   = 1 if A7670 was already powered at cycle start, else 0
//   sh    = STARTING -> SET_HEADERS duration (ms, current cycle)
//   sg    = SET_HEADERS -> GO_SLEEP duration (ms, previous cycle)
//   cf    = consecutive non-200 upload cycles (reset to 0 on a 200)
//   fail  = HTTP code of previous cycle (only when not 200)
//   fstep = protocol step the previous cycle failed at (only when not 200)
class Telemetry {
public:
  // One-shot at boot: reads and clears the MCU reset flags, latching the
  // decoded reset cause. Call before anything else touches the RCC flags.
  void captureResetCause();

  // Latches the firmware version string reported in the `ver` meta field.
  // The pointer must outlive this object (use a string literal).
  void setVersion(const char* version);

  void onCycleStart(bool a7670_powered);
  // Called once per executed state-machine step; tracks the furthest protocol
  // command step reached so a failure can be attributed to it.
  void onStep(Modem_steps step);
  void onSetHeaders();
  void onGoSleep();
  void onCycleResult(int http_code);

  // Returns a pointer to an internal NUL-terminated buffer, valid until
  // the next buildMeta() call.
  const char* buildMeta();

private:
  const char* version = "?";
  const char* reset_cause = "UNK";
  bool a7670_powered_at_start = false;
  unsigned long cycle_start_ms = 0;
  unsigned long set_headers_ms = 0;
  unsigned long t_start_to_set_headers_ms = 0;
  unsigned long t_set_headers_to_go_sleep_ms = 0;
  int last_http_code = 0;
  unsigned consecutive_failures = 0;
  Modem_steps last_protocol_step = STARTING;
  Modem_steps fail_step = STARTING;
  char meta_buffer[256] = { 0 };
};
