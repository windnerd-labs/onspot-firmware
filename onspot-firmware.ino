/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Arduino.h"
#include "Windnerd_Core.h"
#include "WtpBinaryPayload.h"
#include "A7670Manager.h"
#include "ModemStateMachine.h"
#include "PowerManager.h"
#include "Telemetry.h"
#include "PressureSensor.h"
#include "MS5607.h"
#include "StatusIndicator.h"
#include "Logging.h"
#include <Wire.h>

#include "stm32g0xx_hal.h"  // necessary to change clock settings

#define MODEM_RESET_TIMEOUT_MN 20
#define MCU_RESET_TIMEOUT_MN 60  // last-resort: full MCU reset if no upload succeeds for this long
#define SOFT_VERSION "0.1.1"

//#define APN "hologram"

A7670Manager A7670_manager;
PowerManager power_manager(PA0);
// state machine driving the modem upload sequence
ModemStateMachine sm;
Telemetry telemetry;
StatusIndicator status_indicator;  // LED on PA1, open-drain to GND

HardwareSerial SerialDebug(USART1);
HardwareSerial SerialA7670(USART2);

WN_Core Anemometer(PA5, PA6, PB1, PB6, PB7);
WtpBinaryPayload Wtp_payload;

// Sensor connector uses I2C2 (SDA=PA11, SCL=PA12, native AF, no SYSCFG remap).
// The same pads can alternatively carry USART1 RX/TX via the PA11_RMP/PA12_RMP
// remap (PA9_R/PA10_R) — see setup() for the absent-sensor fallback that
// hands the pads from Wire2 over to SerialDebug.
TwoWire Wire2;
WN_I2C sensor_i2c(Wire2);

// Pressure sensor candidates. Add new drivers (e.g. BMP580) to this list and
// the probe in setup() will pick whichever one ACKs on the bus.
MS5607 ms5607_driver(&sensor_i2c, 0x77);
PressureSensor* const pressure_sensor_candidates[] = { &ms5607_driver };
PressureSensor* pressure_sensor = nullptr;

unsigned long last_uploading_time = 0;
unsigned long last_uploading_success_time = 0;  // updated only on a genuine upload success
unsigned long last_modem_reset_time = 0;        // throttles the modem hard-reset
bool is_first_cycle_after_reset = true;
bool was_modem_started = false;

#define RX_BUFFER_LENGTH 128
char rx_buffer[RX_BUFFER_LENGTH];
unsigned rx_pointer = 0;

void sendCommandToModem(const char* cmd) {
  SerialA7670.println(cmd);
  LOG(LOG_D, "MDM", "> %s", cmd);
}

// Non-blocking state machine that drives the modem upload sequence.
// Each step registers its own success/timeout targets via the sm helpers so
// processModemStateMachine() can return immediately and be called again on the next loop tick.
void processModemStateMachine() {

  if (!sm.shouldExecute()) return;

  telemetry.onStep(sm.current());

  switch (sm.current()) {

    case STARTING:

      telemetry.onCycleStart(A7670_manager.isPoweredOn());
      A7670_manager.http_action_result = -1;
      power_manager.evaluate();

      if (is_first_cycle_after_reset) {
        is_first_cycle_after_reset = false;
        // Don't kick off the init sequence on a low battery, the modem being not powered up. Idle until the
        // next cycle
        if (power_manager.shouldPowerOn()) {
          sm.waitFixed(START_FROM_OFF, 0);
        } else {
          sm.setCurrent(SLEEP);
        }
        break;
      }

      // if modem was OFF, we have to turn it ON and take a specific path for initialization
      if (!A7670_manager.isPoweredOn()) {

        // we don't try to power up the modem if battery voltage is too low
        if (power_manager.shouldPowerOn()) {
          LOG(LOG_I, "SYS", "modem=power_on");
          A7670_manager.powerOnOrReset();
          // +CGEV: ME PDN ACT indicates the PDP data context is active;
          sm.waitForResponse("+CGEV:", 30000, START_FROM_OFF, START_FROM_OFF);
        } else {
          sm.setCurrent(SLEEP);
        }
        break;
      }

      sendCommandToModem("AT");
      sm.waitForResponse("OK", 2000, REQUEST_TIME, REQUEST_TIME);
      break;

    case START_FROM_OFF:

#ifdef APN
      sm.waitFixed(SET_APN, 0);
#else
      sm.waitFixed(ENABLE_TIME, 0);
#endif
      break;

#ifdef APN
    case SET_APN:
      char buffer[64];
      sprintf(buffer, "AT+CGDCONT=1,\"IP\",\"%s\"", APN);
      sendCommandToModem(buffer);
      sm.waitForResponse("OK", 5000, ENABLE_TIME, ENABLE_TIME);
      break;

#endif
    case ENABLE_TIME:
      sendCommandToModem("AT+CTZU=1");
      sm.waitForResponse("OK", 1000, REQUEST_TIME, REQUEST_TIME);
      break;

    case REQUEST_TIME:
      sendCommandToModem("AT+CCLK?");
      sm.waitForResponse("+CCLK", 1000, REQUEST_RSSI, REQUEST_RSSI);  // response parsed in A7670manager
      break;

    case REQUEST_RSSI:
      sendCommandToModem("AT+CSQ");
      sm.waitForResponse("+CSQ", 1000, TERMINATE_HTTP, TERMINATE_HTTP);  // response parsed in A7670manager
      break;

    case TERMINATE_HTTP:
      sendCommandToModem("AT+HTTPTERM");
      sm.waitForResponse("OK", 1000, INIT, INIT);
      break;

    case INIT:
      sendCommandToModem("AT+HTTPINIT");
      sm.waitForResponse("OK", 2000, SET_URL, INIT_RETRY);
      break;

    case INIT_RETRY:  // sometimes a first HTTPINIT command returns error but retrying works
      sendCommandToModem("AT+HTTPINIT");
      sm.waitForResponse("OK", 2000, SET_URL, SET_URL);
      break;

    case SET_URL:
      sendCommandToModem("AT+HTTPPARA=\"URL\",\"http://wtp.windnerd.net/bin\"");
      sm.waitForResponse("OK", 2000, SET_APPLICATION_TYPE, SET_APPLICATION_TYPE);
      break;

    case SET_APPLICATION_TYPE:
      sendCommandToModem("AT+HTTPPARA=\"CONTENT\",\"application/octet-stream\"");
      sm.waitForResponse("OK", 1000, SET_HEADERS, SET_HEADERS);
      break;
    case SET_HEADERS:
      {
        // Build the payload first so calculatePayloadLength() is accurate for the HTTPDATA byte-count parameter.
        Wtp_payload.reset();
        Wtp_payload.setAnemometer(&Anemometer);

        unsigned period_mn;
        if (last_uploading_success_time == 0) {
          period_mn = 1;  // ensure we never try after boot to reach samples that don't exist
        } else {
          // Use the actual elapsed time since the last successful upload to determine how far back in time we should send the wind data
          period_mn = max(1u, (unsigned)((millis() - last_uploading_success_time + 30000UL) / 60000UL));
        }

        if (period_mn > 10) {  // protocol limit
          period_mn = 10;
        }

        Wtp_payload.setSampleCount(period_mn * 20);
        Wtp_payload.setTimestamp(A7670_manager.getUnixTime());
        Wtp_payload.setVoltage(power_manager.getVoltage());

        if (pressure_sensor && pressure_sensor->read()) {
          Wtp_payload.setPressure(pressure_sensor->getPressure());
          Wtp_payload.setTemperature(pressure_sensor->getTemperature());
        }

        int rssi = A7670_manager.getRssi();  // dBm
        if (rssi != A7670Manager::RSSI_UNKNOWN) {
          Wtp_payload.setRSSI(rssi);
        }

        telemetry.onSetHeaders();
        Wtp_payload.setMeta(telemetry.buildMeta());

        char buffer[32];
        sprintf(buffer, "AT+HTTPDATA=%d,10000", Wtp_payload.calculatePayloadLength());
        sendCommandToModem(buffer);
        sm.waitForResponse("DOWNLOAD", 5000, SET_DATA, ON_FAILED);
        break;
      }

    case SET_DATA:
      Wtp_payload.sendPayload(&SerialA7670);
      sm.waitForResponse("OK", 2000, POST, ON_FAILED);
      break;

    case POST:
      sendCommandToModem("AT+HTTPACTION=1");                            // perform POST request
      sm.waitForResponse("+HTTPACTION:", 8000, ON_SUCCESS, ON_FAILED);  // response parsed in A7670manager
      break;

    case ON_SUCCESS:
      if (A7670_manager.http_action_result == 200) {
        last_uploading_success_time = millis();
      }
      telemetry.onCycleResult(A7670_manager.http_action_result);
      sm.waitFixed(GO_SLEEP, 0);
      break;

    case ON_FAILED:
      LOG(LOG_W, "NET", "cycle=failed http=%d", A7670_manager.http_action_result);
      telemetry.onCycleResult(A7670_manager.http_action_result);
      sm.waitFixed(GO_SLEEP, 0);
      break;

    case GO_SLEEP:
      telemetry.onGoSleep();
      // Hard-reset the modem if no upload has succeeded for MODEM_RESET_TIMEOUT_MN
      // minutes, throttled to fire at most once per window
      if (millis() - last_uploading_success_time > MODEM_RESET_TIMEOUT_MN * 60000UL
          && millis() - last_modem_reset_time > MODEM_RESET_TIMEOUT_MN * 60000UL) {
        last_modem_reset_time = millis();  // don't reset at every cycle
        A7670_manager.powerOnOrReset();
      } else if (power_manager.shouldPowerOff()) {
        sendCommandToModem("AT+CPOF");
      } else {
        sendCommandToModem("AT+CSCLK=2");
      }
      sm.setCurrent(SLEEP);
      break;

    case SLEEP:
      // re-entry handled in loop() via the upload-interval check
      break;
  }
}

// watchdog activation sequence given in STM32G0 reference manual 28.3.2
void initWatchdog() {
  IWDG->KR = 0xCCCC;
  IWDG->KR = 0x5555;
  IWDG->PR = 0x4;      // prescaler 64
  IWDG->RLR = 0x0800;  // (64 * 2048) / 32000 ~= 4 sec
  while (IWDG->SR != 0) {
  }
  IWDG->KR = 0xAAAA;
}

static inline void feedWatchdog(void) {
  IWDG->KR = 0xAAAA;
}


void setup() {
  // Latch the reset cause from the RCC flags before anything else touches them.
  telemetry.captureResetCause();
  telemetry.setVersion(SOFT_VERSION);

  initWatchdog();
  delay(200);

  status_indicator.begin();

  SerialA7670.setTx(PA2);
  SerialA7670.setRx(PA3);
  SerialA7670.begin(115200);

  Anemometer.setSpeedLedDrive(LED_OPEN_DRAIN);
  Anemometer.setNorthLedDrive(LED_OPEN_DRAIN);
  Anemometer.begin();

  // USART1 is always started so SerialDebug.print() calls elsewhere remain
  // safe (an unstarted HardwareSerial would queue bytes and eventually block
  // forever when the TX buffer fills).
  SerialDebug.begin(115200);

  // Probe the sensor connector via I2C2 (native AF on PA11/PA12). begin()
  // unconditionally takes the pad AF, so if no sensor answers we have to
  // actively hand the pads back to USART1 for SerialDebug to work.
  sensor_i2c.begin(PA11, PA12);  // scl, sda
  pressure_sensor = probePressureSensor(
    pressure_sensor_candidates,
    sizeof(pressure_sensor_candidates) / sizeof(pressure_sensor_candidates[0]));
  status_indicator.setPressureSensorPresent(pressure_sensor != nullptr);

  if (!pressure_sensor) {
    // Release I2C2 (deinits peripheral, reverts PA11/PA12 to analog input),
    // then re-init USART1 on the PA9_R/PA10_R remap which lands on the same pads.
    Wire2.end();
    SerialDebug.end();
    SerialDebug.setTx(PA9_R);
    SerialDebug.setRx(PA10_R);
    SerialDebug.begin(115200);
  }

  last_uploading_time = millis();  // during setup sampling may not be effective yet

  LOG(LOG_I, "SYS", "setup=done");
}

void loop() {

  feedWatchdog();

  // If no upload has succeeded for MCU_RESET_TIMEOUT_MN
  // minutes, reboot the whole MCU. Runs every loop, independent of the modem
  // state machine.
  if (millis() - last_uploading_success_time > MCU_RESET_TIMEOUT_MN * 60000UL) {
    NVIC_SystemReset();
  }

  power_manager.loop();
  Anemometer.loop();
  A7670_manager.loop();
  status_indicator.setConnectivityState(A7670_manager.getConnectivityState());
  status_indicator.setCanPowerOn(power_manager.shouldPowerOn());
  status_indicator.loop();

  // we delay the inital modem power on, after power up it might need some time to be ready for PWKEY action
  if (!was_modem_started && millis() > 10000) {
    was_modem_started = true;
    if (power_manager.shouldPowerOn()) {
      A7670_manager.powerOnOrReset();
    }
  }

  // parse lines sent by modem ouput and pass them to modules interested with them
  while (SerialA7670.available()) {
    char c = SerialA7670.read();
    if (c == '\n') {
      rx_buffer[rx_pointer] = '\0';
      A7670_manager.onLineReceived(rx_buffer);
      sm.onLineReceived(rx_buffer);
      rx_pointer = 0;
    } else {
      if (rx_pointer < RX_BUFFER_LENGTH - 1) {
        rx_buffer[rx_pointer++] = c;
      } else {
        LOG(LOG_W, "SYS", "rxbuf=overflow");
        rx_pointer = 0;
      }
    }
  }

  // start the process for a new upload when time interval from last upload has expired
  if ((sm.current() == SLEEP || sm.current() == OFF) && millis() - last_uploading_time > (power_manager.getPeriod() * 60UL * 1000UL)) {
    LOG(LOG_I, "SYS", "upload=start");
    sm.setCurrent(STARTING);
    last_uploading_time = millis();
  }


  processModemStateMachine();

  // put the MCU to sleep, the WindNerd Core library uses a timer interrupt to wake it up automatically every 100ms
  HAL_PWR_EnterSLEEPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
}

// this is an override to set the SYS clock at 8MHz in order to reduce power consumption
// this code was generated by STM32CubeMx
void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
  RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };

  /** Configure the main internal regulator output voltage
     */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
     * in the RCC_OscInitTypeDef structure.
     */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV2;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
     */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) {
    Error_Handler();
  }

  SystemCoreClockUpdate();
}
