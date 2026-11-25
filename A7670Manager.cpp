#include "A7670Manager.h"
#include "Logging.h"

// Civil date (y, m, d) -> days since 1970-01-01. Howard Hinnant's algorithm.
static uint32_t days_from_civil(int y, unsigned m, unsigned d) {
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  return (uint32_t)((int32_t)era * 146097 + (int32_t)doe - 719468);
}

// Copies an AT response value into a fixed buffer, dropping the characters the
// telemetry meta encoding reserves: commas become '|', separators and anything
// outside printable ASCII are skipped. Always NUL-terminates.
static void copySanitised(char* dst, uint8_t dst_len, const char* src) {
  uint8_t n = 0;
  for (; *src != '\0' && n < dst_len - 1; src++) {
    char c = *src;
    if (c == ',') c = '|';
    else if (c < ' ' || c > '~' || c == ';' || c == '=') continue;
    dst[n++] = c;
  }
  dst[n] = '\0';
}

// Steps past an AT response prefix ("+CPSI:") and the spaces after it.
static const char* afterPrefix(const char* line, uint8_t prefix_len) {
  const char* p = line + prefix_len;
  while (*p == ' ') p++;
  return p;
}

#define A7670_RST PA7
#define A7670_PWR_KEY PB0
#define A7670_STATUS PA4

extern HardwareSerial SerialA7670;
extern HardwareSerial SerialDebug;



enum A7670_POWER_STATES {
  POWER_UNKNOWN,
  POWERED_OFF,
  POWERING_ON,
  POWERED_ON,
  RESETTING
};


unsigned long power_key_pulse_start;
unsigned long reset_pulse_start;



A7670_POWER_STATES A7670_power_state = POWER_UNKNOWN;

A7670Manager::A7670Manager() {

  digitalWrite(A7670_RST, HIGH);
  digitalWrite(A7670_PWR_KEY, HIGH);
  pinMode(A7670_RST, OUTPUT_OPEN_DRAIN);
  pinMode(A7670_PWR_KEY, OUTPUT_OPEN_DRAIN);

  digitalWrite(A7670_RST, HIGH);
  digitalWrite(A7670_PWR_KEY, HIGH);


  pinMode(A7670_STATUS, INPUT);
  A7670_power_state = digitalRead(A7670_STATUS) ? POWERED_ON : POWERED_OFF;
}


bool A7670Manager::isPoweredOn() {
  return A7670_power_state == POWERED_ON;
}

int A7670Manager::getRssi() const {
  return rssi;
}

const char* A7670Manager::getCpsi() const {
  return cpsi;
}

const char* A7670Manager::getFirmwareVersion() const {
  return fw_version;
}

uint32_t A7670Manager::getUnixTime() const {
  if (cclk_unix_time == 0) return 0;
  return cclk_unix_time + (millis() - cclk_anchor_ms) / 1000;
}

bool A7670Manager::readA7670StatusPin() {
  return digitalRead(A7670_STATUS) == HIGH;
}

void A7670Manager::releaseControlPins() {
  digitalWrite(A7670_PWR_KEY, HIGH);
  digitalWrite(A7670_RST, HIGH);
}

void A7670Manager::powerOnOrReset() {

  // Modem is about to be reset or just powered on — any prior SIM / PDP
  // state is stale until the new URCs arrive
  conn_state = ConnectivityState::UNKNOWN;
  cpsi[0] = '\0';
  fw_version[0] = '\0';

  if (readA7670StatusPin()) {
    digitalWrite(A7670_RST, LOW);
    LOG(LOG_I, "MDM", "rst=low");

    reset_pulse_start = millis();
    A7670_power_state = RESETTING;
  } else {

    digitalWrite(A7670_PWR_KEY, LOW);
    LOG(LOG_I, "MDM", "pwrkey=low");

    power_key_pulse_start = millis();
    A7670_power_state = POWERING_ON;
  }
}


void A7670Manager::onLineReceived(const char* line) {
  LOG(LOG_D, "MDM", "< %s", line);

  if (strncmp(line, "+CPIN:", 6) == 0) {
    if (strstr(line, "READY")) {
      // Don't pull back from NETWORK_OK: a live network already proves the SIM.
      if (conn_state != ConnectivityState::NETWORK_OK) {
        conn_state = ConnectivityState::SIM_READY;
      }
      LOG(LOG_I, "MDM", "sim=ready");

    } else if (strstr(line, "REMOVED")) {
      conn_state = ConnectivityState::SIM_REMOVED;
      LOG(LOG_W, "MDM", "sim=removed");
    } else if (strstr(line, "SIM P")) {
      // covers "SIM PIN", "SIM PUK", "SIM PIN2", "SIM PUK2"
      conn_state = ConnectivityState::SIM_PIN_REQUIRED;
      LOG(LOG_W, "MDM", "sim=pin_required");
    }
  } else if (strncmp(line, "+CGEV:", 6) == 0) {
    // PDP context / GPRS attach lifecycle URC. "ME PDN ACT" (and "NW PDN ACT")
    // mean the modem reached the data network. NETWORK_OK latches, so DEACT /
    // DETACH are ignored — we only care that the network came up at least once.
    // Check DEACT before ACT — "DEACT" contains "ACT" as a substring.
    if (strstr(line, "DEACT") || strstr(line, "DETACH")) {
      LOG(LOG_I, "NET", "pdp=down_ignored");
    } else if (strstr(line, "ACT")) {
      conn_state = ConnectivityState::NETWORK_OK;
      LOG(LOG_I, "NET", "pdp=up");
    }
  } else if (strncmp(line, "+HTTPACTION", 10) == 0) {
    LOG(LOG_D, "NET", "http=action");

    http_action_result = -1;
    http_body_length = -1;
    if (sscanf(line, "+HTTPACTION: %*d,%d,%d", &http_action_result, &http_body_length) >= 1) {
      if (http_action_result == 200) {
        conn_state = ConnectivityState::NETWORK_OK;
      }

      LOG(LOG_I, "NET", "http=%d len=%d", http_action_result, http_body_length);
    }
  } else if (strncmp(line, "+HTTPREAD:", 10) == 0) {
    // "+HTTPREAD: <n>" announces n raw body bytes; "+HTTPREAD: 0" ends the read
    int n;
    if (sscanf(line, "+HTTPREAD: %d", &n) == 1 && n > 0) {
      raw_pending = (uint16_t)n;
    }
  } else if (strncmp(line, "+CSQ:", 5) == 0) {
    int csq;
    if (sscanf(line, "+CSQ: %d,%*d", &csq) == 1) {
      // CSQ reports a 0..31 signal index (99 = not detectable). Convert to dBm
      // per 3GPP TS 27.007: dBm = -113 + 2*csq, i.e. -113 dBm (0) .. -51 dBm (31).
      rssi = (csq == 99) ? RSSI_UNKNOWN : (-113 + 2 * csq);
      LOG(LOG_I, "NET", "rssi=%d", rssi);
    }
  } else if (strncmp(line, "+CPSI:", 6) == 0) {
    // Kept whole rather than parsed: the field list varies with radio
    // technology and firmware revision
    copySanitised(cpsi, CPSI_BUF_LEN, afterPrefix(line, 6));
    LOG(LOG_I, "NET", "cpsi=%s", cpsi);
  } else if (strncmp(line, "+CGMR:", 6) == 0) {
    // Modem firmware revision. Reported once per power cycle and latched, so
    // later cycles can put it in the telemetry meta without re-querying.
    copySanitised(fw_version, FW_BUF_LEN, afterPrefix(line, 6));
    LOG(LOG_I, "MDM", "fw=%s", fw_version);
  } else if (strncmp(line, "+CCLK:", 6) == 0) {
    int yy, mo, dd, hh, mm, ss, tz_q;
    if (sscanf(line, "+CCLK: \"%d/%d/%d,%d:%d:%d%d\"",
               &yy, &mo, &dd, &hh, &mm, &ss, &tz_q)
        == 7) {
      uint32_t days = days_from_civil(2000 + yy, (unsigned)mo, (unsigned)dd);
      int32_t local = (int32_t)days * 86400 + hh * 3600 + mm * 60 + ss;
      cclk_unix_time = (uint32_t)(local - tz_q * 15 * 60);
      cclk_anchor_ms = millis();
      LOG(LOG_I, "MDM", "unix=%lu", (unsigned long)cclk_unix_time);
    }
  }
}


void A7670Manager::onRawByte(uint8_t b) {
  if (raw_pending == 0) return;
  raw_pending--;
  // Bytes beyond the buffer are consumed and dropped
  if (http_body_len < HTTP_BODY_BUF_LEN) {
    http_body[http_body_len++] = b;
  }
}

const uint8_t* A7670Manager::getHttpBody(uint8_t* len) const {
  *len = http_body_len;
  return http_body;
}

void A7670Manager::clearHttpBody() {
  http_body_len = 0;
  raw_pending = 0;
}


void A7670Manager::loop() {

  if (!readA7670StatusPin() && A7670_power_state == POWERED_ON) {
    delay(1);
    if (!readA7670StatusPin()) {
      LOG(LOG_I, "MDM", "power=off");
      A7670_power_state = POWERED_OFF;
    }

    return;
  }

  if (readA7670StatusPin() && A7670_power_state == POWERED_OFF) {
    delay(1);
    if (readA7670StatusPin()) {
      LOG(LOG_I, "MDM", "power=on");
      A7670_power_state = POWERED_ON;

    }

    return;
  }

  if (A7670_power_state == RESETTING) {
    if (millis() - reset_pulse_start > 2000 && readA7670StatusPin()) {  // typical reset pulse is 1.5s, we apply some margin
      digitalWrite(A7670_RST, HIGH);
      LOG(LOG_I, "MDM", "rst=high");
      A7670_power_state = POWERED_ON;
    }

    return;
  }


  if (A7670_power_state == POWERING_ON) {
    if (readA7670StatusPin()) {
      delay(1);
      if (readA7670StatusPin()) {
        // if (millis() - power_key_pulse_start > 100) {
        digitalWrite(A7670_PWR_KEY, HIGH);
        LOG(LOG_I, "MDM", "pwrkey=high");
        A7670_power_state = POWERED_ON;
      }
    }
    return;
  }
}