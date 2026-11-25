/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <Arduino.h>

// Hibernation: MCU in STOP1, woken every HIBERNATE_WAKEUP_S by the RTC wakeup
// timer clocked by LSI, only to feed the watchdog. After the requested number
// of wakeups the device resets, so nothing has to be restored and the boot
// path takes over (see docs/PROG-DEBUG.md).
//
// The periodic wakeups are required: the IWDG runs from LSI, which stays on in
// STOP, and can't be stopped once started (freezing it in STOP needs the
// IWDG_STOP option bit, not worth reprogramming option bytes in the field).

#define HIBERNATE_WAKEUP_S 2  // must stay below the IWDG timeout (~4 s)

static inline void feedWatchdog(void) {
  IWDG->KR = 0xAAAA;
}

// Never returns: sleeps for `wakeups` periods of HIBERNATE_WAKEUP_S, then
// resets the MCU. The caller powers the modem off and turns the LEDs off first.
[[noreturn]] void hibernate(uint32_t wakeups);

// Call once at boot. Returns true when this boot is the end of a hibernation,
// and stops the RTC wakeup timer left running by it.
bool hibernationWoke();
