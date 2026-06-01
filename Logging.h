/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <Arduino.h>

// Unified debug logging over SerialDebug.
//
// Every line has the form:
//
//     <ms> <LVL> <TAG> <body>
//
//   ms    millis() since boot (uptime, monotonic, gives ordering + timing)
//   LVL   one of E W I D (error / warn / info / debug)
//   TAG   3-letter module tag: SYS MDM NET PWR ...
//   body  free text, ideally key=value pairs ("v=3.92 tier=2")
//
// The format is human-readable on a live monitor and trivially machine-parsed:
//     ^(\d+) ([EWID]) (\w{3}) (.*)$
//
// Modem traffic uses direction markers in the body: "> ..." for TX (host->modem)
// and "< ..." for RX (modem->host).

enum LogLevel { LOG_E = 0, LOG_W = 1, LOG_I = 2, LOG_D = 3 };

// Compile-time floor: lines above this level are dropped (and cost no flash for
// the format string args). Set to LOG_I for production to silence debug spam.
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_D
#endif

extern HardwareSerial SerialDebug;

static const char LOG_LVL_CH[] = { 'E', 'W', 'I', 'D' };

// LOG(level, "TAG", fmt, ...) — emits exactly one line.
// fmt/args are printf-style. Note: %f requires float printf support in the
// build; prefer integer fields (e.g. millivolts) to stay portable.
#define LOG(level, tag, ...)                                            \
  do {                                                                  \
    if ((level) <= LOG_LEVEL) {                                         \
      char _logbuf[96];                                                 \
      int _n = snprintf(_logbuf, sizeof(_logbuf), "%lu %c %s ",         \
                        millis(), LOG_LVL_CH[level], tag);              \
      if (_n > 0 && _n < (int)sizeof(_logbuf)) {                        \
        snprintf(_logbuf + _n, sizeof(_logbuf) - _n, __VA_ARGS__);      \
      }                                                                 \
      SerialDebug.println(_logbuf);                                     \
    }                                                                   \
  } while (0)
