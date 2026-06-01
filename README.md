# WindNerd OnSpot

**Live wind, wherever you need it.**

**Release expected in July 2026**


WindNerd OnSpot is a sensor board for [3D printed anemometer](https://github.com/windnerd-labs/Anemometer-3D-files) that measures **wind speed
and direction** and sends it straight to [windnerd.net](https://windnerd.net) — all on its own, over the cellular
network. Insert a SIM card, mount it, switch it on, and your wind data starts flowing.

- **Autonomous** — built-in cellular, standard SIM card; works anywhere there's a mobile signal.
- **Without compromise** — accurate wind speed and direction, sampled continuously and reported in high definition.
- **Set and forget** — runs on solar/battery, adapting how often it uploads to preserve power.
- **Optional pressure and temperature sensor** — an optional temperature/pressure sensor can be plugged onto the board. The temperature reading is not accurate in sun, but it can give a useful indication of the temperature at night.


---

## You don't need this repository to use WindNerd OnSpot

This repository holds the **open source firmware** that runs inside the device. It's published for the advanced users who like to look under the hood or want to adapt it for another platform or other needs.

If an update is ever needed for your device, you will be able to reprogram the firmware with the provided USB tool through a simple web application.


---
---

# For testers & developers

The rest of this document is the get-started reference for people running a test unit or
building the firmware. Firmware version: **`D-0.0.5`**.

- [1. First power-on](#1-first-power-on)
- [2. Status-light codes](#2-status-light-codes)
- [3. Troubleshooting](#3-troubleshooting)
- [4. Reading the serial log](#4-reading-the-serial-log)
- [5. Build & flash](#5-build--flash)
- [6. How it works](#6-how-it-works)

## 1. First power-on
1. **Insert an active SIM with a data plan** (no PIN lock).
2. **Connect to the Sunbank 2W with the provided cable** and make sure the battery is charged.
3. **Power on.** The red status light starts a repeating burst pattern — at first **6 flashes** (modem starting up). The other green lights let you check whether the speed and direction magnets are correctly detected.
4. **Connecting.** As the modem registers you'll see **3 flashes** (SIM ready, network
   not yet up).
5. **Running.** Once it reaches the network and is sending wind data, the burst drops to
   **1 flash** (or 2 — see the table). That's the healthy steady state.

From then on the unit samples the wind continuously and uploads in batches every few
minutes (more often on a full battery, less often as it drains).


## 2. Status-light codes

Every ~3 seconds the single status LED emits a **burst of short flashes**. The **number
of flashes** is the status code (source of truth:
[`StatusIndicator.cpp`](StatusIndicator.cpp)):

| Flashes | Meaning | Healthy? |
| :---: | --- | :---: |
| **1** | Online with optional sensor detected | ✅ |
| **2** | Online, but no optional sensor was detected (wind works) | ✅ |
| **3** | SIM ready, **not yet connected** to the network (registering, or no coverage) | ⏳ |
| **4** | **SIM locked** — PIN/PUK required | ❌ |
| **5** | **No SIM detected** | ❌ |
| **6** | Modem **not responding / still starting up** | ⏳ |


**One pattern overrides all of the above:**

| Pattern | Meaning |
| --- | --- |
| **Slow on/off blink** (~2 s on, 2 s off) | **Battery too low** to power the modem — charge the device |

A burst of 1 or 2 flashes is the normal running state. 3 and 6 are transient during start-up and should resolve within a minute or two; if they persist, see below.

## 3. Troubleshooting

| You see | Likely cause | What to do |
| --- | --- | --- |
| **4 flashes** | SIM has a PIN/PUK lock | Use a SIM with the PIN disabled |
| **5 flashes** | SIM missing or not seated | Insert / reseat the SIM |
| **3 flashes that never reach 1–2** | No cellular registration | Check the SIM is active and there's signal where the unit sits |
| **6 flashes that never change** | Modem not coming up | Power-cycle; if it persists, report it |
| **Slow on/off blink** | Battery below the minimum operating tier | Charge the device |
| **Light never turns on** | No power / dead battery | Charge, then power-cycle |
| **1–2 flashes (looks healthy) but no data on windnerd.net** | SIM registers on the network, but the **data plan is expired/depleted** so uploads are rejected | Check the SIM's data balance/plan and top it up |

The firmware is self-healing: if no upload succeeds for ~20 minutes it hard-resets the modem, and after ~60 minutes it reboots the whole unit. So a unit stuck on 3 or 6 will
keep retrying on its own. If a fault keeps recurring, **report it** with the flash count
and, if you can capture it, the serial log (below).

## 4. Reading the serial log

Connect the programming tool to the board, set its slider switch to the debug position and power up the board. Open the recognized virtual serial port at 115200 baud, for example: `minicom -D /dev/ttyUSB0 -b 115200`

```
<ms> <LVL> <TAG> <body>
```

- `<ms>` — milliseconds since boot
- `<LVL>` — `E` error · `W` warning · `I` info · `D` debug
- `<TAG>` — module: `SYS`, `MDM` (modem), `NET` (network), `PWR` (power)
- `<body>` — `key=value` details (modem traffic uses `>` for TX and `<` for RX)


## 5. Build & flash

This is an **Arduino** project for an **STM32G0K8Ux** microcontroller. The sketch entry point is [`onspot-firmware.ino`](onspot-firmware.ino); all sources live alongside it in the repo root.
Programming is done through the classic STM32 serial bootloader.

### Prerequisites

- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)
- [Arduino IDE](https://www.arduino.cc/en/software) (or `arduino-cli`) with the
  **[STM32 core (STM32duino)](https://github.com/stm32duino/Arduino_Core_STM32)**
  installed via the Boards Manager.
- The **WindNerd Core** library, installed via the Arduino Library Manager.
- The programming tool provided with the board.

### Steps

1. Open `onspot-firmware.ino` in the Arduino IDE.
2. Select **STM32 MCU-based boards** under *Tools → Board*.
3. Select **STM32G0K8Ux** board under *Tools → Board → STM32 boards*, and the
   correct upload method (STM32CubeProgrammer serial).
4. Set the programming tool's slider switch to the prog position.
5. Connect the prog tool to the board.
6. Power up the OnSpot board. If it booted correctly into programming (bootloader) mode, no LEDs should turn on. If they do, reset the board with the push button until it starts in programming mode.


## 6. How it works

Wind samples are packed into the compact **WTP-bin** binary format and signed with
HMAC-Blake2s before upload — the format is documented in
[`WTP-bin.md`](WTP-bin.md). Each device has its own unique ID and signing key programmed into the OTP area.
Upload frequency adapts to battery voltage
(more frequent when full, backing off as it drains), and a watchdog plus staged
modem/MCU resets keep a unit recovering on its own.

| File | Responsibility |
| --- | --- |
| [`onspot-firmware.ino`](onspot-firmware.ino) | Setup, main loop, upload state machine |
| [`A7670Manager`](A7670Manager.cpp) | SIMCom A7670 LTE modem: power, SIM/network state, time, RSSI |
| [`ModemStateMachine`](ModemStateMachine.cpp) / [`ModemSteps.h`](ModemSteps.h) | Step sequencing for the upload cycle |
| [`WtpBinaryPayload`](WtpBinaryPayload.cpp) | Builds the signed WTP-bin frame |
| [`Telemetry`](Telemetry.cpp) | Collect diagnostic info to be uploaded in the `meta` field |
| [`PowerManager`](PowerManager.cpp) | Battery voltage tiers → upload period & power mode |
| [`StatusIndicator`](StatusIndicator.cpp) | Status-LED flash-count patterns |
| [`MS5607`](MS5607.cpp) | Optional barometric/temp sensor|
| [`Logging.h`](Logging.h) | Leveled serial logging |
| `blake2*` | BLAKE2s used to sign uploaded data |

---

## License

BSD 3-Clause — see [LICENSE.txt](LICENSE.txt). © 2026 windnerd.net.
Third-party notices (BLAKE2 reference implementation) are included in that file.
