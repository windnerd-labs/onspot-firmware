#ifndef MODEM_STEPS_H
#define MODEM_STEPS_H

// SET_APN is always part of the enum (regardless of #ifdef APN) so the
// underlying values stay consistent across translation units.
enum Modem_steps {
  STARTING = 0,
  START_FROM_OFF,
  SET_APN,
  ENABLE_TIME,
  REQUEST_TIME,
  REQUEST_RSSI,
  TERMINATE_HTTP,
  INIT,
  INIT_RETRY,
  SET_URL,
  SET_HEADERS,
  SET_APPLICATION_TYPE,
  SET_DATA,
  POST,
  ON_SUCCESS,
  ON_FAILED,
  GO_SLEEP,
  SLEEP,
  OFF
};

#endif
