# 🖱️ ESP32 BLE Precision Trackpad ⚡

Turn a salvaged laptop touchpad (**Synaptics TM3096** with the **T1321B** IC) into a wireless Bluetooth multi-touch **Windows Precision Touchpad (PTP)** using an ESP32.

---

## ✨ What This Project Does

Instead of emulating a generic mouse with clunky software gesture timers, this project talks directly to the Synaptics ASIC over **PS/2 Absolute Mode** and exposes an authentic **Windows Precision Touchpad (PTP) HID Digitizer** over Bluetooth Low Energy (BLE).

Windows recognizes the ESP32 as a genuine Precision Touchpad (`Settings > Bluetooth & devices > Touchpad`). All multi-touch gestures — smooth kinetic scrolling, pinch-to-zoom, 3-finger app switching, and tap-to-click — are handled natively by Windows OS.

---

## 🎯 Hardware Target: Synaptics TM3096 (T1321B IC)

* 🏷️ **Touchpad Model:** Synaptics TM3096
* 🧠 **Controller IC:** Synaptics T1321B (salvaged from an old Dell laptop)
* 📡 **Microcontroller:** ESP32 (WROOM-32 / Dev Module)
* 🔌 **Bus Connection:** 4 wires (3.3V, GND, CLK on GPIO 16, DATA on GPIO 17) + two 10kΩ pull-up resistors to 3.3V

---

## 🚀 Current Capabilities of the Latest Version (`src/esp32_precision_trackpad`)

The production firmware (`src/esp32_precision_trackpad/esp32_precision_trackpad.ino` / `Bluetooth_trackpad_ps2_v1.ino`) implements:

* ⚡ **Hardware PS/2 Absolute Mode (`0xC1`):** Sends sliced mode commands via `0xE8` to configure the Synaptics ASIC into high-rate 6-byte absolute packet streaming.
* ⏱️ **Interrupt-Driven PS/2 Core:** Uses hardware pin interrupts (`ps2ClkISR` on falling edge) feeding a volatile 64-byte circular buffer, eliminating dropped clock pulses while the ESP32 BLE stack is transmitting.
* 🖐️ **Multi-Finger Tracking via AGM:** Decodes Synaptics Advanced Gesture Mode (AGM) packets where $W=2$ conveys secondary finger coordinates ($X_2, Y_2$), synchronized with primary packets ($W=0$ for 2 fingers, $W=1$ for 3 fingers, $W \ge 4$ for 1 finger).
* 📐 **Coordinate Mapping:** Translates coordinates ($0 \dots 8191$) and inverts the Y-axis (`8191 - Y`) so the touchpad bottom-left origin matches Windows top-left screen coordinates.
* 🪟 **Native Windows PTP HID Descriptor:** Exposes a custom Digitizer report (`Usage Page 0x0D`, `Usage 0x05`) with:
  - 3 simultaneous finger contact collections (Tip Switch, Confidence flag, Contact IDs).
  - 16-bit Scan Time timestamp (100 µs units).
  - Contact Count.
  - Physical Left and Right button reporting.
  - Maximum Contact Count Feature Report (Report ID 2 = 3).
  - PTP Certification Feature Report (Report ID 3, 256-byte blob) for Windows driver compatibility.
* 👆 **Contact Lift Reporting:** Dispatches explicit lift reports (`Tip=0, Conf=0`) with the specific Contact ID when fingers leave the pad.
* 🏎️ **BLE Transmission Rate Limiting:** Throttles output to ~80 Hz (12 ms) to prevent BLE stack buffer saturation and latency lag, while immediately flushing urgent events (finger release or button click) with zero delay.
* 💡 **Status LED Indication:** GPIO 2 blinks while advertising/disconnected and stays solid blue once paired.

---

## 🛠️ Real Difficulties & How We Fixed Them

Salvaging an undocumented OEM laptop trackpad and turning it into a wireless precision device was a gritty hardware reverse-engineering process. Here are the exact engineering problems we ran into and how we solved each one:

### 1. 🔍 Reverse-Engineering the Unknown Pinout (Data, PS/2 & I2C Bus Pins)
* ❌ **The Problem:** The TM3096 was harvested from an old Dell laptop with no ribbon cable, no mating connector, and no schematics. All online pinout guides for Synaptics touchpads claimed Pin 1 = GND and Pin 2 = VCC. Following them gave a dead board.
* 📍 **How We Located Ground:** Using a multimeter in continuity mode against the large exposed copper ground plane and metal shielding, we probed each connector pin and test pad. Only **Pin 4** beeped — proving Pin 4 was the true GND and internet pinouts were wrong.
* ⚡ **Tracing Data & Clock Lines:** With the touchpad unpowered, we switched the multimeter to diode test mode (red probe on GND). The signal pins revealed themselves by measuring the forward voltage drop of the chip's internal ESD protection diodes (~0.5V to 0.7V).
* 🔀 **PS/2 vs. I2C Bus Pins:** The T1321B is a dual-personality chip capable of both SMBus/I2C and PS/2. We built an I2C scanner that detected the chip at address `0x2C`. However, reading input reports from the HID descriptor returned static zeros (`01 00 00 00`) because modern laptop I2C modes require proprietary ACPI host power management and GPIO interrupt handshakes. We traced the alternate bus pins to the PS/2 lines, where PS/2 Absolute Mode worked predictably and required zero vendor drivers.

### 2. ⚡ Finding the Safe Operating Voltage (Unknown Power Rail)
* ⚠️ **The Problem:** Laptop trackpads can run on 5V, 3.3V, or even 1.8V internal logic rails. Applying 5V to a 3.3V ASIC risks instantly destroying the chip, while under-powering it keeps the microcontroller in a brownout reset loop.
* 🔬 **How Safe Power Was Verified:** We inspected the passive decoupling capacitors near the T1321B ASIC and probed the `T20` test pad. By tracing the power plane directly to the input of the onboard regulator, we confirmed that the chip runs natively at **3.3V**. Powering it directly from the ESP32's onboard 3.3V regulator rail ensured safe operation and eliminated the need for external level shifters.

### 3. 📉 Data Line Noise & Parity Errors (Why We Needed Pull-Up Resistors)
* 📡 **The Problem:** PS/2 is an open-collector bus — devices only pull lines to ground; they cannot drive them high. Without external pull-up resistors, the floating signal wires acted like antennas, picking up EMI and capacitive noise from jumper wires. This caused phantom clock edges, framing errors, and parity mismatch errors (`ps2ReadByte` returning -1 and -4).
* 🚫 **Why Internal Pull-Ups Failed:** The ESP32's internal pull-ups (`INPUT_PULLUP` ~45kΩ) were far too weak. The RC time constant resulted in sluggish rise times (>15 µs), rounding off the digital edges and corrupting data.
* 🧮 **Choosing the Right Resistor Value:**
  - *1kΩ tested:* Forced the tiny open-drain FETs on the Synaptics chip to sink 3.3 mA per line ($6.6\text{ mA}$ continuous), causing heating and wasting power.
  - *10kΩ chosen:* Provided sharp ~1 µs rise times at the ~12 kHz PS/2 clock frequency, creating clean square waves and completely eliminating bus noise and parity failures.

### 4. 🐛 Fixing the Infuriating Windows Stuttering & Latency Bugs
Getting raw data packets into Windows was only half the battle. The initial prototypes suffered from severe stuttering, cursor teleporting, and sluggish input lag. We tracked down and eliminated four distinct causes:

* 🛑 **Stutter Bug A: Packet Drops from BLE CPU Preemption**
  - *Symptom:* The cursor frequently froze for split seconds or jumped across the screen.
  - *Root Cause:* In polling mode, whenever the ESP32's dual-core FreeRTOS BLE stack took CPU time to manage radio buffers, the main loop missed the touchpad's falling clock edges (~12 kHz). A single missed bit threw off the 6-byte packet alignment.
  - *Fix:* Replaced polling with a hardware interrupt (`attachInterrupt` on CLK falling edge) with an `IRAM_ATTR` ISR feeding into a 64-byte volatile circular buffer (`ps2Buf`). The interrupt handler captures every bit in microseconds without dropping data during BLE broadcasts.

* ⏳ **Stutter Bug B: BLE Queue Saturation & "Rubber-Band" Latency**
  - *Symptom:* The cursor felt sluggish and had momentum lag — continuing to move for 200–400 ms after your finger stopped.
  - *Root Cause:* Transmitting every decoded PS/2 packet straight over BLE flooded the ESP32 BLE notification queue, creating severe backlog latency.
  - *Fix:* Added an ~80 Hz (12 ms) rate limiter for continuous movement, while giving finger release events (`Tip=0`) and button clicks `urgent` priority so they flush immediately with zero delay.

* 🌀 **Stutter Bug C: AGM Multi-Touch Desynchronization Judder**
  - *Symptom:* Two-finger scrolling stuttered violently and pinch-to-zoom was erratic and jittery.
  - *Root Cause:* In Synaptics Absolute Mode, multi-finger coordinates are split across two sequential packets: an Advanced Gesture Mode (AGM) packet ($W=2$) carrying Finger 2, followed by a primary packet ($W=0$ or $1$) carrying Finger 1. Sending reports on every raw packet caused Windows to receive alternating 1-finger and 2-finger states.
  - *Fix:* The firmware buffers the AGM packet ($W=2$) and only dispatches the unified PTP report to Windows when the primary packet arrives (`if (!gTouch.isAgm) sendPtpReport()`), ensuring both finger positions update synchronously in a single frame.

* 👻 **Stutter Bug D: Phantom Touches on Finger Lift**
  - *Symptom:* Cursor stayed frozen in place or kept clicking when the finger was lifted.
  - *Root Cause:* Abruptly stopping transmission when pressure dropped ($Z < 12$) left Windows with no contact release event, assuming the finger was still held down.
  - *Fix:* Implemented active contact lifecycle tracking (`f1_active`, `f2_active`, `f3_active`). When pressure drops below threshold, the firmware sends an explicit lift report with `Tip=0, Conf=0` for each active contact ID, and then goes completely silent during idle.

---

## 🔌 Wiring Diagram

```
         Synaptics TM3096 (T1321B)
        +-------------------------+
        |                         |
        |  [VCC]  [GND] [CLK] [DATA]
        +---+-------+-----+-----+--+
            |       |     |     |
            |       |   +-+-+ +-+-+
            |       |   |10k| |10k|   Pull-up resistors
            |       |   +-+-+ +-+-+
            |       |     |     |
            |       |     +--+--+
            |       |        |
            v       v        v
          3.3V     GND      3.3V
            ^       ^
            |       |     +---------+
            |       |     |  GPIO16 | (PS/2 CLK)
            |       |     |  GPIO17 | (PS/2 DATA)
            |       |     |  GPIO2  │ (Status LED)
            +-------+-----+---------+
                        ESP32
```

| Touchpad Pin / Pad | ESP32 Pin | Details |
| :--- | :--- | :--- |
| **VCC** | `3.3V` | Direct 3.3V power (no series resistor) |
| **GND** | `GND` | Common ground (Pin 4 on connector / metal shield) |
| **CLK** | `GPIO 16` | PS/2 Clock line + 10kΩ pull-up to 3.3V |
| **DATA** | `GPIO 17` | PS/2 Data line + 10kΩ pull-up to 3.3V |
| **LED** | `GPIO 2` | Onboard status indicator |

---

## 📂 Repository Structure

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
│       └── esp32_precision_trackpad.ino   # 🏆 Production PTP firmware
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

## 💻 How to Flash & Use

1. 📥 Open `src/esp32_precision_trackpad/esp32_precision_trackpad.ino` in Arduino IDE.
2. 📦 Ensure you have the **ESP32** board package installed (`esp32` by Espressif Systems).
3. ⚙️ Select your board (e.g., **ESP32 Dev Module**) and your COM port.
4. ⬆️ Click **Upload**.
5. 🔵 Once uploaded, the onboard blue LED on `GPIO 2` will blink slowly.
6. 📶 On Windows, go to **Settings > Bluetooth & devices > Add device > Bluetooth**.
7. 🖱️ Select **Precision Touchpad**.
8. ✅ Once paired, the LED stays solid blue. Check **Settings > Touchpad** and verify:
   > *"Your PC has a precision touchpad."*
