# Programming & Debugging

## 1. The programming tool

The programming tool provided with the board is a **USB-serial converter with
2 V logic levels**. It shows up on the computer as a standard virtual serial
port (e.g. `/dev/ttyUSB0` on Linux, `COMx` on Windows).

- It is used both for flashing (through the STM32 serial bootloader) and for
  reading the serial log.
- The board's UART runs at 2 V: a generic 3.3 V or 5 V USB-serial adapter is
  not a drop-in substitute.
- Match the pins when connecting: pins 1–6 of the programming tool go to
  pins 1–6 of the board.

### Entering bootloader mode

1. Connect the programming tool to the board **before** powering it.
2. Power up the OnSpot board. If it has booted correctly into programming
   (bootloader) mode, no LEDs turn on. If they do, reset the board with the
   push button until it starts in bootloader mode.


## 2. Flash a release binary

### Prerequisites

- [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)
- The programming tool provided with the board.

### Steps

1. Download `onspot-firmware-vX.Y.Z.bin` from the
   [releases](https://github.com/windnerd-labs/onspot-firmware/releases) page.
2. Put the board in bootloader mode (see [above](#entering-bootloader-mode)).
3. In STM32CubeProgrammer, choose **UART**, select the tool's serial port and
   click **Connect**.
4. In *Erasing & Programming*, pick the `.bin` file, set the start address to
   **`0x08000000`**, tick **Verify programming** and **Run after programming**,
   then click **Start Programming**.

The device ID and signing key live in the OTP area, which flashing does not
touch: the board keeps its identity across updates.


## 3. Build & flash from source

This is an **Arduino** project for an **STM32G0K8Ux** microcontroller. The sketch entry point is [`onspot-firmware.ino`](../onspot-firmware.ino); all sources live alongside it in the repo root.
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
4. Put the board in bootloader mode (see [above](#entering-bootloader-mode)).
5. Click **Upload**.


## 4. Reading the serial log

1. Plug the programming tool into the computer and open the recognized virtual
   serial port at 115200 baud, for example: `minicom -D /dev/ttyUSB0 -b 115200`
2. Only once the serial port is open, connect the programming tool to the board
   while the board is already powered (hot plug), so that it does not enter
   bootloader mode.

The serial port must be opened **before** hot plugging.

```
<ms> <LVL> <TAG> <body>
```

- `<ms>` — milliseconds since boot
- `<LVL>` — `E` error · `W` warning · `I` info · `D` debug
- `<TAG>` — module: `SYS`, `MDM` (modem), `NET` (network), `PWR` (power)
- `<body>` — `key=value` details (modem traffic uses `>` for TX and `<` for RX)


## 5. How it works

Wind samples are packed into the compact **WTP-bin** binary format and signed with
HMAC-Blake2s before upload — the format is documented in
[`WTP-bin.md`](../WTP-bin.md). Each device has its own unique ID and signing key programmed into the OTP area.
Upload frequency adapts to battery voltage
(more frequent when full, backing off as it drains), and a watchdog plus staged
modem/MCU resets keep a unit recovering on its own.

**Hibernation.** The device hibernates when the server reply asks for it (the
`hibernate` running mode) or when the battery drops below 3.55 V. The modem is
powered off and the MCU enters STOP mode, waking every 2 s only to feed the
watchdog.

| File | Responsibility |
| --- | --- |
| [`onspot-firmware.ino`](../onspot-firmware.ino) | Setup, main loop, upload state machine |
| [`A7670Manager`](../A7670Manager.cpp) | SIMCom A7670 LTE modem: power, SIM/network state, time, RSSI |
| [`ModemStateMachine`](../ModemStateMachine.cpp) / [`ModemSteps.h`](../ModemSteps.h) | Step sequencing for the upload cycle |
| [`WtpBinaryPayload`](../WtpBinaryPayload.cpp) | Builds the signed WTP-bin frame |
| [`Telemetry`](../Telemetry.cpp) | Collects diagnostic info to be uploaded in the `meta` field |
| [`PowerManager`](../PowerManager.cpp) | Battery voltage tiers → upload period & power mode |
| [`Hibernation`](../Hibernation.cpp) | STOP mode with RTC wakeups on LSI, reset at the end |
| [`StatusIndicator`](../StatusIndicator.cpp) | Status-LED flash-count patterns |
| [`MS5607`](../MS5607.cpp) | Optional barometric/temp sensor|
| [`Logging.h`](../Logging.h) | Leveled serial logging |
| `blake2*` | BLAKE2s used to sign uploaded data |
