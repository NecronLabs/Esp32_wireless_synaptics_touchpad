# ESP32 BLE Precision Trackpad

Turn a salvaged laptop touchpad (**Synaptics TM3096** with the **T1321B** IC) into a wireless Bluetooth multi-touch **Windows Precision Touchpad (PTP)** using an ESP32.

---

## What This Project Does

Instead of emulating a basic mouse wheel with choppy software gestures, this project interfaces directly with the Synaptics chip over **PS/2 Absolute Mode** and exposes a **native Windows Precision Touchpad (PTP) digitizer** over Bluetooth Low Energy (BLE).

Windows recognizes the ESP32 as an authentic Precision Touchpad (`Settings > Bluetooth & devices > Touchpad`). All gestures — smooth kinetic scrolling, pinch-to-zoom, 3-finger virtual desktop switching, and tap-to-click — are processed natively by Windows.

---

## Hardware Target: Synaptics TM3096 (T1321B IC)

* **Touchpad Model:** Synaptics TM3096
* **Controller IC:** Synaptics T1321B (salvaged from an old Dell laptop)
* **Microcontroller:** ESP32 (WROOM-32 / Dev Module)
* **Connection:** 4 wires (3.3V, GND, CLK, DATA) + two 10kΩ pull-up resistors

---

## Current Capabilities of the Latest Version (`src/esp32_precision_trackpad`)

The production firmware (`src/esp32_precision_trackpad/esp32_precision_trackpad.ino`) implements:

* **Hardware PS/2 Absolute Mode (`0xC1`):** Sends sliced mode commands via `0xE8` to configure the Synaptics ASIC into high-rate 6-byte absolute streaming mode.
* **Interrupt-Driven PS/2 Core:** Uses hardware pin interrupts (`ps2ClkISR` on falling edge) feeding an in-memory 64-byte circular buffer, eliminating dropped bytes while the ESP32's dual-core BLE radio is broadcasting.
* **Multi-Finger Tracking via AGM:** Decodes Synaptics Advanced Gesture Mode (AGM) packets where $W=2$ conveys secondary finger coordinates ($X_2, Y_2$), paired with primary packets ($W=0$ for 2 fingers, $W=1$ for 3 fingers, $W \ge 4$ for 1 finger).
* **Coordinate Mapping:** Translates coordinates ($0 \dots 8191$) and inverts the Y-axis (`8191 - Y`) so the touchpad bottom-left origin matches Windows top-left screen coordinates.
* **Native Windows PTP HID Descriptor:** Exposes a custom Digitizer report (`Usage Page 0x0D`, `Usage 0x05`) with:
  - 3 simultaneous finger contact collections (Tip Switch, Confidence flag, Contact IDs).
  - 16-bit Scan Time timestamp (100 µs units).
  - Contact Count.
  - Physical Left and Right button reporting.
  - Maximum Contact Count Feature Report (Report ID 2 = 3).
  - PTP Certification Feature Report (Report ID 3, 256-byte blob) for Windows driver compatibility.
* **Contact Lift Reporting:** Dispatches lift reports (`Tip=0, Conf=0`) with the specific Contact ID when fingers leave the pad.
* **BLE Transmission Rate Limiting:** Throttles output to ~80 Hz (12 ms) to prevent BLE stack buffer saturation and latency lag, while immediately flushing urgent events (finger release or button click) with zero delay.
* **Status LED Indication:** GPIO 2 blinks while advertising/disconnected and stays solid blue once paired.

---

## Difficulties & Engineering Lessons Learned

Getting a salvaged OEM touchpad running on a microcontroller without documentation required working through several real-world hardware traps:

### 1. The Missing Pinout & Mystery 8-Pin FPC Connector
* **Problem:** Harvested from an old Dell laptop with no ribbon cable or pinout diagram. Online pinout guides claimed Pin 1 = GND and Pin 2 = VCC. Following them gave zero response.
* **Solution:** Continuity testing with a multimeter against the grounded metal backing revealed that **only Pin 4 was GND**. With real GND found, the remaining pads could be tested safely.

### 2. The 1kΩ Series Resistor Trap (Voltage Starvation)
* **Problem:** A 1kΩ resistor was initially inserted in series with the 3.3V power wire as a "safety buffer" to prevent burning the board during pin testing.
* **Symptom:** Both CLK and DATA lines stayed stuck LOW even with pull-up resistors installed.
* **Discovery:** The touchpad capacitive sensing circuitry draws 20–40 mA. By Ohm's law ($V = I \times R$), a 20 mA load through a 1kΩ resistor drops 20V! The chip received near 0V, never booted, and left the open-drain bus lines unreleased.
* **Fix:** Removed the 1kΩ series resistor and connected 3.3V directly once GND was verified.

### 3. Open-Collector Bus & Resistor Selection
* **Problem:** PS/2 lines are open-collector. Only 1kΩ and 10kΩ resistors were available.
* **Why 1kΩ was rejected:** Sinking 3.3V through 1kΩ forces the chip's open-drain FETs to sink 3.3 mA per line ($6.6\text{ mA}$ continuous), creating unnecessary heat and draining battery.
* **Why 10kΩ was chosen:** At PS/2 clock rates (~12 kHz), 10kΩ provides sharp, reliable rise times with minimal power consumption.

### 4. Reversed Wires & Power-On Delay
* **Problem:** Initial readings showed CLK HIGH and DATA LOW. Swapping lines inverted the symptom.
* **Discovery:** PS/2 devices run an internal **Basic Assurance Test (BAT)** upon boot, holding the DATA line low for up to 500–1500 ms before releasing it. The code was bailing out too early. Adding a 1500 ms startup settling window resolved the false error.

### 5. Why We Bypassed I2C / SMBus
* **The Detour:** Modern Dell laptops often run Synaptics touchpads over SMBus/I2C. We built I2C scanner sketches and found the chip at address `0x2C`.
* **The Roadblock:** While the device acknowledged on the bus, reading input reports returned static zeros (`01 00 00 00`). HID-over-I2C requires complex ACPI power sequencing and host-driven interrupt handshakes.
* **The Decision:** PS/2 Absolute Mode is self-clocking, predictable, and requires no proprietary OS drivers. We pivoted back to PS/2 and unlocked full multi-touch capabilities.

---

## Wiring Diagram

```
         Synaptics TM3096 (T1321B)
        ┌─────────────────────────┐
        │                         │
        │  [VCC]  [GND] [CLK] [DATA]
        └───┬───────┬─────┬─────┬─┘
            │       │     │     │
            │       │  ┌──┴──┐ ┌┴───┐
            │       │ 10kΩ  10kΩ    Pull-up resistors
            │       │  └──┬──┘ └┬───┘
            │       │     └──┬──┘
            │       │        │
            ▼       ▼        ▼
          3.3V     GND      3.3V
            ▲       ▲
            │       │     ┌─────────┐
            │       │     │  GPIO16 │ (PS/2 CLK)
            │       │     │  GPIO17 │ (PS/2 DATA)
            │       │     │  GPIO2  │ (Status LED)
            └───────┴─────┴─────────┘
                        ESP32
```

| Touchpad Pin / Pad | ESP32 Pin | Details |
| :--- | :--- | :--- |
| **VCC** | `3.3V` | Direct 3.3V (no series resistors) |
| **GND** | `GND` | Common ground (Pin 4 on connector) |
| **CLK** | `GPIO 16` | PS/2 Clock line + 10kΩ pull-up to 3.3V |
| **DATA** | `GPIO 17` | PS/2 Data line + 10kΩ pull-up to 3.3V |
| **LED** | `GPIO 2` | Onboard status indicator |

---

## Repository Structure

```
esp32-ble-precision-trackpad/
├── .github/
│   ├── ISSUE_TEMPLATE/
│   │   ├── bug_report.md
│   │   └── feature_request.md
│   └── workflows/
│       └── arduino-ci.yml
├── src/
│   └── esp32_precision_trackpad/
│       └── esp32_precision_trackpad.ino   # 🏆 Final production PTP firmware
├── test/
│   ├── ps2_diagnostics/                   # PS/2 bus test & reset sketches
│   │   ├── esptrackpad_diagnostic/
│   │   ├── ps2_touchpad_test/
│   │   └── uno_diagnostic/
│   ├── i2c_experiments/                   # I2C scanning & HID experiments
│   │   ├── i2c_touchpad_test/
│   │   ├── i2c_scanner_esp32/
│   │   └── i2c_scanner_uno/
│   └── legacy_mouse_emulation/            # Phase 2 mouse emulation sketch
│       └── ble_trackpad_mouse/
├── docs/
│   ├── hardware_wiring.md                 # Detailed pinout & electrical guide
│   └── protocol_notes.md                  # PS/2 packet decoding & PTP architecture
├── res/
│   └── i2c_test_terminal_output.png       # Test terminal capture from I2C trials
├── .gitignore
├── CHANGELOG.md
├── CODE_OF_CONDUCT.md
├── CONTRIBUTING.md
├── LICENSE
└── README.md
```

---

## How to Flash & Use

1. **Open the project in Arduino IDE (v2.x recommended).**
2. Install the **ESP32** board package (`esp32` by Espressif Systems via Boards Manager).
3. Open `src/esp32_precision_trackpad/esp32_precision_trackpad.ino`.
4. Select your board (e.g., **ESP32 Dev Module**) and your COM port.
5. Click **Upload**.
6. Once uploaded, the onboard blue LED on `GPIO 2` will blink slowly.
7. On your PC, open **Windows Settings > Bluetooth & devices > Add device > Bluetooth**.
8. Click **Precision Touchpad**.
9. Once paired, the LED stays solid blue. Open **Settings > Touchpad** and verify:
   > *"Your PC has a precision touchpad."*
