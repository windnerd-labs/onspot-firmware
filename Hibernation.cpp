/*
 * Copyright (c) 2026, windnerd.net
 * All rights reserved.
 *
 * This source code is licensed under the BSD 3-Clause License found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Hibernation.h"
#include "stm32g0xx_hal.h"

// Left in a backup register (survives the reset) to tell the next boot it is
// the end of a hibernation
#define HIBERNATE_MAGIC 0x48494230UL  // "HIB0"

// RTC/16 from the 32 kHz LSI
#define WAKEUP_TIMER_HZ 2000UL

static void enableBackupDomainAccess() {
  RCC->APBENR1 |= RCC_APBENR1_PWREN | RCC_APBENR1_RTCAPBEN;
  PWR->CR1 |= PWR_CR1_DBP;
}

static void stopWakeupTimer() {
  RTC->WPR = 0xCA;
  RTC->WPR = 0x53;
  RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
  RTC->SCR = RTC_SCR_CWUTF;
  RTC->WPR = 0xFF;
}

void hibernate(uint32_t wakeups) {

  // Silence every interrupt source but the RTC. Nothing is restored on the
  // way out: the device resets.
  __disable_irq();
  SysTick->CTRL = 0;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  NVIC->ICER[0] = 0xFFFFFFFF;
  NVIC->ICPR[0] = 0xFFFFFFFF;
  EXTI->IMR1 &= ~0xFFFFUL;  // GPIO lines: rotor pulses must not wake the MCU

  // Clock the RTC from LSI. LSI is already running, the IWDG forces it on.
  // RTCSEL can only be changed after a backup domain reset.
  enableBackupDomainAccess();
  if ((RCC->BDCR & RCC_BDCR_RTCSEL) != RCC_BDCR_RTCSEL_1) {
    RCC->BDCR |= RCC_BDCR_BDRST;
    RCC->BDCR &= ~RCC_BDCR_BDRST;
    RCC->BDCR |= RCC_BDCR_RTCSEL_1;
  }
  RCC->BDCR |= RCC_BDCR_RTCEN;

  // Periodic wakeup timer
  RTC->WPR = 0xCA;
  RTC->WPR = 0x53;
  RTC->CR &= ~(RTC_CR_WUTE | RTC_CR_WUTIE);
  while (!(RTC->ICSR & RTC_ICSR_WUTWF)) {
  }
  RTC->WUTR = HIBERNATE_WAKEUP_S * WAKEUP_TIMER_HZ - 1;
  RTC->CR &= ~RTC_CR_WUCKSEL;  // 000: RTC/16
  RTC->SCR = RTC_SCR_CWUTF;
  RTC->CR |= RTC_CR_WUTIE | RTC_CR_WUTE;
  RTC->WPR = 0xFF;

  EXTI->IMR1 |= EXTI_IMR1_IM19;  // RTC wakeup
  NVIC_EnableIRQ(RTC_TAMP_IRQn);

  // PRIMASK is set: the pending RTC interrupt ends WFI without running a
  // handler, it is cleared here. Only RTC wakeups are counted, so a stray
  // wake can't shorten the hibernation.
  uint32_t count = 0;
  while (count < wakeups) {
    feedWatchdog();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    if (RTC->SR & RTC_SR_WUTF) {
      RTC->SCR = RTC_SCR_CWUTF;
      count++;
    }
    NVIC_ClearPendingIRQ(RTC_TAMP_IRQn);
  }

  TAMP->BKP0R = HIBERNATE_MAGIC;
  // The IWDG keeps running through the reset: give the next boot a full
  // timeout to reach initWatchdog(), not what is left of the last wakeup
  feedWatchdog();
  NVIC_SystemReset();
}

bool hibernationWoke() {
  enableBackupDomainAccess();
  bool woke = TAMP->BKP0R == HIBERNATE_MAGIC;
  TAMP->BKP0R = 0;
  if (RCC->BDCR & RCC_BDCR_RTCEN) {
    stopWakeupTimer();
  }
  PWR->CR1 &= ~PWR_CR1_DBP;
  return woke;
}
