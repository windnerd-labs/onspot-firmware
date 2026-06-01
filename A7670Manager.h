#pragma once
#include <Arduino.h>

// Modem connectivity ladder, from no SIM up to a live data network. Each rung
// subsumes the ones below it: NETWORK_OK implies the SIM is fine, so a single
// value is enough to drive the status indicator.
//
// SIM rungs come from +CPIN URCs; NETWORK_OK is reached on the first successful
// PDP/data event this power cycle and then latches — transient data drops
// (+CGEV DEACT/DETACH) do not pull it back down. A power-off or reset clears it
// to UNKNOWN.
enum class ConnectivityState : uint8_t {
  UNKNOWN,           // modem off, or powered on but not yet reported readiness
  SIM_REMOVED,
  SIM_PIN_REQUIRED,  // SIM PIN / PUK lock — user intervention needed
  SIM_READY,         // SIM ready, modem can attempt network registration
  NETWORK_OK         // data network reached at least once this power cycle
};

class A7670Manager {
public:
  A7670Manager();
  void powerOnOrReset();
  void loop();
  void onLineReceived(const char* line);
  bool isPoweredOn();
  int getRssi() const;
  uint32_t getUnixTime() const;
  ConnectivityState getConnectivityState() const {
    return conn_state;
  }
  int http_action_result = -1;

  // Sentinel returned by getRssi() when the modem reports no measurable signal
  // (CSQ 99). Matches WtpBinaryPayload::RSSI_UNKNOWN.
  static const int RSSI_UNKNOWN = 0x7F;

private:
  bool readA7670StatusPin();

  int rssi = RSSI_UNKNOWN;
  uint32_t cclk_unix_time = 0;
  uint32_t cclk_anchor_ms = 0;
  ConnectivityState conn_state = ConnectivityState::UNKNOWN;
};
