# Rowing Audio Remote

BLE media remote for a rowing machine: two buttons, two LEDs, mounted where you can hit them mid-stroke to skip tracks or change volume without touching your phone. Runs on an ESP32-C3 and shows up as a standard Bluetooth HID device — no companion app needed.

<p align="center">
  <img src="images/front.jpg" width="45%" alt="Front"/>
  <img src="images/back.jpg" width="45%" alt="Back"/>
</p>

## Features

- Advertises as a BLE HID consumer-control device (`Rowing Remote v2`) — pairs like a keyboard, works with any OS/phone that supports BLE HID media keys
- Two modes, toggled by holding both buttons for 1s:
  - **Mode 1**: prev track / next track
  - **Mode 2**: volume down / volume up
- Debounced buttons, keys fire on release
- LEDs flash twice on BLE connect and on mode switch, and light solid while their button is held

## Hardware

- ESP32-C3 dev board
- 2x momentary push buttons
- 2x LED + ~220-330Ω resistor each
- Enclosure: see [`models/`](models/) for the 3D-printable case

| Function | GPIO |
|---|---|
| Button — prev / vol down | 7 |
| Button — next / vol up | 8 |
| LED — pairs with GPIO7 button | 21 |
| LED — pairs with GPIO8 button | 20 |

Wiring: button GPIO → button → GND (internal pullup, active-low). LED GPIO → resistor → LED anode, cathode → GND (active-high). Both polarities are configurable in the sketch if your wiring differs.

## Install

1. [Arduino IDE](https://www.arduino.cc/en/software) → **File > Preferences** → add this to *Additional Board Manager URLs*:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. **Tools > Board > Boards Manager** → install `esp32` (Espressif Systems)
3. **Sketch > Include Library > Manage Libraries** → install **NimBLE-Arduino** (v2.x)
4. **Tools > Board** → pick your ESP32-C3 dev board
5. Open `RowingMachine.ino`, connect the board over USB, hit **Upload**

## Usage

Power it on, pair `Rowing Remote v2` from your phone/laptop's Bluetooth settings like any keyboard. Tap a button to send the mapped media key. Hold both buttons for 1s to switch modes (LEDs flash to confirm).

To change pins, timings, or the device name, edit the `CONFIG` block at the top of `RowingMachine.ino`.

## License

MIT — see [LICENSE](LICENSE).
