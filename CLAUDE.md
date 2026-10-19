# CLAUDE.md — ESP32 Wireless Synaptics Precision Touchpad (TM3096 / T1321B)

> **Document Purpose:** Complete technical reference, architectural blueprint, history of decisions, debugging logs, configuration parameters, and roadmap for the ESP32 BLE Precision Touchpad project.

---

## 1. Executive Summary

This project reverse-engineers a laptop touchpad (**Synaptics TM3096** powered by the **Synaptics T1321B** ASIC, salvaged from an older Dell laptop) and interfaces it with an **ESP32** to create an authentic wireless **Bluetooth Windows Precision Touchpad (PTP)**.

Unlike typical DIY projects that emulate a traditional relative mouse wheel with artificial software gesture timers, this firmware operates the Synaptics chip in **PS/2 Absolute Mode (`0xC1`)** and reports multi-touch digitizer coordinates directly to Windows using the **Microsoft Precision Touchpad HID specification**. Windows recognizes the device natively (`Settings > Bluetooth & devices > Touchpad`), providing true OS-level inertial kinetic scrolling, pinch-to-zoom, 3/4-finger desktop switching, and palm rejection with zero external drivers.

* **GitHub Repository:** [https://github.com/NecronLabs/Esp32_wireless_synaptics_touchpad](https://github.com/NecronLabs/Esp32_wireless_synaptics_touchpad)
* **Latest Production Release:** `v1.0.0`
* **Primary Production Firmware:** `src/esp32_precision_trackpad/esp32_precision_trackpad.ino` (mirrored in `Bluetooth_trackpad_ps2_v1/Bluetooth_trackpad_ps2_v1.ino`)

---

## 2. Chronological History & The Engineering Odyssey

### 2.1 The Initial Prototype & The 1kΩ Series Resistor Trap
* **Initial State:** User possessed a bare Synaptics TM3096 touchpad salvaged from a Dell laptop without a ribbon cable, connector pinout, or schematics. The user guessed wiring based on generic online articles and connected CLK to TX2 (GPIO 17) and DATA to RX2 (GPIO 16) with a 1kΩ series resistor on VCC to "protect the board."
* **The Failure:** The diagnostic sketch reported `CLK idle: LOW` and `DATA idle: LOW`. The bus was completely dead.
* **The Physics / Root Cause:** Capacitive sensing touchpads draw 20 mA to 50 mA during operation. According to Ohm's Law ($V = I \times R$), a 20 mA current across a 1kΩ series resistor drops:
  $$V_{\text{drop}} = 0.020\,\text{A} \times 1000\,\Omega = 20\,\text{V}$$
  Because the ESP32 supply is only 3.3V, the touchpad received virtually 0V. The internal microcontroller never booted and kept the open-drain bus lines unreleased.
* **The Fix:** Removed the series resistor entirely and connected the ESP32 3.3V pin directly to the touchpad VCC pad.

### 2.2 Sizing the Open-Collector Pull-Up Resistors
* **The Requirement:** PS/2 is an open-collector (open-drain) bus. Both the touchpad and microcontroller only pull lines LOW; external resistors must pull lines HIGH to 3.3V.
* **The Inventory Dilemma:** The user only had 1kΩ and 10kΩ resistors.
* **Why 1kΩ was rejected:** Sinking 3.3V through 1kΩ forces the chip's internal open-drain FETs to sink:
  $$I = \frac{3.3\,\text{V}}{1000\,\Omega} = 3.3\,\text{mA per line} \quad (6.6\,\text{mA continuous})$$
  This stresses the miniature silicon transistors on the T1321B, creates localized heating, and causes unnecessary battery drain.
* **Why 10kΩ was selected:** At standard PS/2 clock speeds (~10–16 kHz, period ~60–100 µs), line capacitance is ~50–100 pF. The resulting RC time constant is $\tau = R \times C \approx 10\,\text{k}\Omega \times 100\,\text{pF} = 1\,\mu\text{s}$. This rise time is well within the 60 µs clock period, producing crisp digital square waves while drawing only 0.33 mA during LOW states.
* **Why Internal Pull-Ups Failed:** ESP32 internal pull-ups (`INPUT_PULLUP` ~45kΩ) yielded an RC rise time of >15 µs, rounding clock edges, introducing noise susceptibility, and causing parity check failures (`-4`).

### 2.3 Ground Truth Reverse-Engineering: Finding the Real Pinout
* **Online Pinout Failure:** Generic internet diagrams claimed Pin 1 = GND and Pin 2 = VCC. Following this resulted in floating lines.
* **Multimeter Continuity Discovery:** Probing each pin on the 8-pin FPC connector with a multimeter against the large copper ground plane and metal chassis confirmed that **only Pin 4 is GND**.
* **Diode Mode Signal Probing:** With power disconnected, the multimeter was set to Diode Test mode (red probe on GND, black probe on unknown pins). CMOS protection diodes on signal pins showed a characteristic forward drop of ~0.55V–0.65V. VCC showed open/capacitive charging behavior.

### 2.4 Power-On Basic Assurance Test (BAT) Settling Delay
* **The Phenomenon:** After fixing wiring and pull-ups, CLK read HIGH but DATA stayed persistently LOW.
* **The Cause:** Per the PS/2 specification, upon power-up the device executes an internal self-test (Basic Assurance Test or BAT). During BAT, the touchpad intentionally asserts DATA LOW for up to 500–1500 ms before clocking out the completion byte (`0xAA`). Early diagnostic firmware checked the bus too quickly and aborted.
* **The Fix:** Added an active settling loop that polls the bus for up to 1500 ms upon startup before declaring bus readiness.

### 2.5 The I2C / SMBus Exploration Detour
* **Hypothesis:** Modern Dell laptops (post-2013) commonly run Synaptics trackpads over SMBus/I2C (RMI4 or HID-over-I2C).
* **The Test:** Custom I2C scanner sketches were built for ESP32 and Arduino Uno. An I2C device was successfully detected at address `0x2C`.
* **The Roadblock:** Reading input reports from the HID descriptor yielded static empty reports (`01 00 00 00`, `X=0 Y=0 Z=0`, documented in `Screenshot 2026-08-07 230808.png`). Modern laptop I2C touchpads require host ACPI power state sequencing, GPIO reset pulses, and proprietary register configuration.
* **The Decision:** PS/2 Absolute Mode (`0xC1`) is master-slave synchronous, self-clocking, and completely independent of proprietary ACPI drivers. We pivoted back to PS/2.

### 2.6 Evolution Through Three Firmware Generations
1. **Phase 1 (Diagnostics):** Bit-banged PS/2 reset (`0xFF`), self-test verification (`0xFA 0xAA 0x00`), and relative 3-byte mouse packets (`esptrackpad.ino`, `ps2_touchpad_test.ino`).
2. **Phase 2 (Mouse Emulation):** Activated 6-byte Absolute Mode. Used `BleMouse` library to emulate standard relative mouse deltas, with software gesture detection for 1-finger move, 1-finger tap, 2-finger scroll, 2-finger right-click, tap-and-drag, and momentum glide (`ble_trackpad.ino`, `ble_trackpad_ps2.ino`).
   * *Limitation:* Mouse deltas feel stepped; Windows sees a mechanical mouse wheel; pinch-to-zoom and native 3-finger app switching do not exist.
3. **Phase 3 (Native Precision Touchpad — Current Latest):** Ditched mouse emulation entirely in favor of native **Windows Precision Touchpad (PTP) over BLE HID Digitizer** (`Bluetooth_trackpad_ps2_v1.ino`). Multi-touch coordinates are sent raw to Windows, unlocking native OS gestures.

---

## 3. Hardware Specifications & Pinout

### 3.1 Hardware Components
* **Touchpad Module:** Synaptics TM3096
* **Controller ASIC:** Synaptics T1321B (Dell OEM)
* **Microcontroller:** ESP32-WROOM-32 Development Board
* **Pull-Up Resistors:** 2x 10kΩ 1/4W metal film resistors

### 3.2 Electrical Wiring Diagram

```
                 Synaptics TM3096 (T1321B)
               +---------------------------+
               |                           |
               |   [VCC]  [GND] [CLK] [DATA]
               +-----+------+-----+-----+--+
                     |      |     |     |
                     |      |   +-+-+ +-+-+
                     |      |   |10k| |10k|    External 10kΩ Pull-Ups
                     |      |   +-+-+ +-+-+
                     |      |     |     |
                     |      |     +--+--+
                     |      |        |
                     v      v        v
                   3.3V    GND      3.3V
                     ^      ^
                     |      |     +---------+
                     |      |     |  GPIO16 | (PS/2 CLK / RX2)
                     |      |     |  GPIO17 | (PS/2 DATA / TX2)
                     |      |     |  GPIO2  | (Status LED)
                     +------+-----+---------+
                                ESP32
```

### 3.3 Pin Mapping Table

| Touchpad Pad / FPC Pin | ESP32 Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| **VCC** | `3.3V` | Logic & Sensor Power | Direct connection to ESP32 3.3V rail. **Never use series resistors!** |
| **GND** | `GND` | System Ground | Verified on Pin 4 of the 8-pin FPC connector & metal chassis shield. |
| **CLK** | `GPIO 16` | PS/2 Clock Line | Pulled HIGH to 3.3V via external 10kΩ resistor. Configured as input with falling-edge interrupt. |
| **DATA** | `GPIO 17` | PS/2 Data Line | Pulled HIGH to 3.3V via external 10kΩ resistor. Bidirectional open-drain line. |
| **Onboard LED** | `GPIO 2` | Connection Status | Blinks at 1 Hz when advertising / disconnected; Solid ON when BLE paired. |

---

## 4. Low-Level Protocol & Packet Decoding

### 4.1 Synaptics Absolute Mode Initialization (`0xC1`)
To switch the touchpad from standard relative 3-byte mouse packets to 6-byte absolute coordinate packets, the host transmits a "sliced command" sequence:
1. Issue `0xE6` (Set Scale 1:1)
2. Issue `0xF5` (Disable Data Reporting)
3. Transmit mode byte `0xC1` using the Synaptics sliced argument protocol:
   - The 8-bit mode byte `0xC1` (`1100 0001`b) is split into four 2-bit nibbles.
   - Each 2-bit value is sent as an argument to `0xE8` (Set Resolution):
     ```
     0xE8 -> (mode >> 6) & 0x03  // bits 7-6
     0xE8 -> (mode >> 4) & 0x03  // bits 5-4
     0xE8 -> (mode >> 2) & 0x03  // bits 3-2
     0xE8 -> (mode >> 0) & 0x03  // bits 1-0
     ```
4. Issue `0xF3, 0x14` (Set Sample Rate 20 — Synaptics mode commit sequence)
5. Issue `0xF4` (Enable Data Reporting)

Mode `0xC1` configures:
* Absolute coordinate reporting ($0 \dots 8191$)
* High report rate (80 packets/sec)
* $W$-mode enabled (encodes finger width and multi-touch contact states)

### 4.2 Raw 6-Byte Packet Bit Allocation
Every absolute packet consists of 6 bytes framed by sync bits (`buf[0] & 0xC8 == 0x80` and `buf[3] & 0xC8 == 0xC0`):

```
Byte 0: [ 1   0   W3  W2   1   W1  btnR btnL ]
Byte 1: [ Y11 Y10 Y9  Y8  X11 X10  X9   X8   ]
Byte 2: [ Z7  Z6  Z5  Z4   Z3  Z2  Z1   Z0   ] (Z = Touch Pressure)
Byte 3: [ 1   1   Y12 X12  0   W0  btnM btnM ]
Byte 4: [ X7  X6  X5  X4   X3  X2  X1   X0   ] (X = 13-bit X coordinate)
Byte 5: [ Y7  Y6  Y5  Y4   Y3  Y2  Y1   Y0   ] (Y = 13-bit Y coordinate)
```

#### Coordinate Extraction:
```cpp
x = (uint16_t)(((buf[3] & 0x10) << 8) | ((buf[1] & 0x0F) << 8) | buf[4]);
y = (uint16_t)(((buf[3] & 0x20) << 7) | ((buf[1] & 0xF0) << 4) | buf[5]);
z = buf[2];
w = (uint8_t)(((buf[0] & 0x30) >> 2) | ((buf[0] & 0x04) >> 1) | ((buf[3] & 0x04) >> 2));
```

### 4.3 Multi-Touch AGM (Advanced Gesture Mode) Decoding
The Synaptics T1321B communicates secondary contact coordinates using packet interleaving:
* **$W = 2$ (AGM Packet):** This packet contains the coordinates of **Finger 2** ($X_2, Y_2, Z_2$). It is flagged as `isAgm = true` and buffered in memory.
* **$W = 0$ (Two-Finger Primary Packet):** Indicates 2 fingers are touching. Combined with the previously buffered AGM coordinates.
* **$W = 1$ (Three-Finger Primary Packet):** Indicates 3 fingers are touching. Secondary finger coordinates are assigned to $X_2, Y_2$; tertiary finger is synthesized offset ($X_2 + 200, Y_2$).
* **$W \ge 4$ (Single-Finger Primary Packet):** Single contact active. $W$ represents finger width.
* **$Z < 12$ (`FINGER_Z_OFF`):** Touchpad is released / no active touch.

---

## 5. Windows Precision Touchpad (PTP) HID Architecture

### 5.1 Coordinate Space Normalization
* **Synaptics Hardware Space:** Origin $(0, 0)$ is at the **bottom-left** of the sensor pad. Maximum range is $0 \dots 8191$.
* **Windows PTP Space:** Origin $(0, 0)$ is at the **top-left** of the screen.
* **Coordinate Inversion Formula:**
  $$\text{mappedY} = 8191 - Y_{\text{synaptics}}$$

### 5.2 HID Digitizer Report Descriptor Breakdown
The BLE HID service registers the following descriptor (`Usage Page 0x0D`, `Usage 0x05`):
* **Report ID 1 (Input Report — 25 Bytes):**
  * **Finger 1 Logical Collection:** Tip Switch (1 bit), Confidence (1 bit), Contact ID (6 bits, ID=1), X (16-bit, max 8191), Y (16-bit, max 8191).
  * **Finger 2 Logical Collection:** Tip Switch (1 bit), Confidence (1 bit), Contact ID (6 bits, ID=2), X (16-bit), Y (16-bit).
  * **Finger 3 Logical Collection:** Tip Switch (1 bit), Confidence (1 bit), Contact ID (6 bits, ID=3), X (16-bit), Y (16-bit).
  * **Scan Time:** 16-bit relative timestamp in 100 µs units.
  * **Contact Count:** 8-bit integer representing the number of valid active fingers.
  * **Buttons:** Button 1 (Left click) and Button 2 (Right click), plus 6 bits padding.
* **Report ID 2 (Feature Report):** Contact Count Maximum (reports `0x03` = 3 contacts max).
* **Report ID 3 (Feature Report):** Microsoft PTP Certification status (256-byte zero blob, satisfying Windows driver certification checks).

### 5.3 Memory Structure Layout
```cpp
struct __attribute__((packed)) PtpContact {
  uint8_t  tip_id;  // bit 0: Tip Switch, bit 1: Confidence, bits 2-7: Contact ID
  uint16_t x;       // 16-bit X coordinate (0-8191)
  uint16_t y;       // 16-bit Inverted Y coordinate (0-8191)
};

struct __attribute__((packed)) PtpReport {
  PtpContact fingers[3];   // 3 * 5 = 15 bytes
  uint16_t   scanTime;     // 2 bytes
  uint8_t    contactCount; // 1 byte
  uint8_t    button;       // 1 byte (bit 0: Left, bit 1: Right)
};
```

---

## 6. The Stuttering & Latency Bugs: Root Causes & Solutions

During initial prototype testing, movement on Windows was choppy, suffered from 200–500 ms rubber-band latency, and stuttered during multi-finger gestures. Four distinct bugs were diagnosed and resolved:

### 6.1 Bug A: Packet Loss from FreeRTOS BLE CPU Preemption
* **Root Cause:** In polling mode (`ps2ReadBytePoll()`), the CPU waited in tight loops for clock edges (~12 kHz, ~83 µs cycle). Whenever the ESP32's dual-core FreeRTOS scheduler serviced the Bluetooth stack (BLE connection events, advertising, or ATT packet transmission), the CPU was preempted. Falling clock edges were missed, causing bit misalignment and corrupting the 6-byte packet frame. The cursor would freeze and jump across the screen.
* **The Solution:** Implemented a non-blocking hardware interrupt on the falling edge of `PS2_CLK_PIN` (`GPIO 16`):
  ```cpp
  void IRAM_ATTR ps2ClkISR() {
    uint8_t bit = digitalRead(PS2_DATA_PIN);
    // State machine: start bit (0), 8 data bits, parity bit, stop bit (1)
    // Pushes completed byte into a 64-byte volatile ring buffer
  }
  ```
  The interrupt routine resides in internal IRAM (`IRAM_ATTR`), executing in <2 µs and capturing every clock pulse with zero dropped bits regardless of BLE activity.

### 6.2 Bug B: BLE Notification Queue Congestion & Rubber-Band Latency
* **Root Cause:** PS/2 Absolute Mode streams packets at ~80–100 packets/second. Pushing every packet immediately over BLE notifications overwhelmed the ESP32 BLE stack queue. Packets backed up in memory, causing a 200–500 ms delay where the cursor kept gliding after the finger stopped.
* **The Solution:** Implemented an 80 Hz (12 ms) rate limiter for continuous tracking, combined with an immediate priority bypass for urgent events:
  ```cpp
  bool urgent = (is_idle && !was_idle) || (report.button != last_button);
  if (!urgent && (millis() - last_send < 12)) {
    return; // Rate limit continuous stream
  }
  last_send = millis();
  input->setValue((uint8_t*)&report, sizeof(PtpReport));
  input->notify();
  ```

### 6.3 Bug C: Multi-Touch AGM Desynchronization Judder
* **Root Cause:** Synaptics multi-touch data is delivered across two sequential packets: an AGM packet ($W=2$) containing Finger 2, followed by a primary packet ($W=0$ or $W=1$) containing Finger 1. Dispatching a PTP report on every received packet caused Windows to see alternating 1-finger and 2-finger frames, resulting in violent scroll stutter and broken pinch-to-zoom.
* **The Solution:** The parser buffers the AGM packet ($W=2$) and suppresses BLE dispatch until the companion primary packet arrives:
  ```cpp
  if (!gTouch.isAgm) {
    sendPtpReport(); // Dispatches unified frame containing both finger positions
  }
  ```

### 6.4 Bug D: Phantom Touches on Finger Lift
* **Root Cause:** If packet transmission halted abruptly when finger pressure dropped ($Z < 12$), Windows never received a contact release event. Windows assumed the finger was permanently pressed, causing stuck clicks and preventing cursor movement.
* **The Solution:** Implemented active contact lifecycle tracking (`f1_active`, `f2_active`, `f3_active`). When pressure drops below threshold, the firmware sends an explicit release frame with `Tip=0, Conf=0` for each active Contact ID, before entering idle silence:
  ```cpp
  } else if (f1_active) {
    report.fingers[0].tip_id = 0x00 | (1 << 2); // Tip=0, Conf=0, ID=1
    f1_active = false;
  }
  ```

---

## 7. Codebase Directory Map

```
Project- Wireless Trackpad/
├── esp32-ble-precision-trackpad/          # 🚀 Official Clean Git Repository
│   ├── .github/
│   │   ├── ISSUE_TEMPLATE/
│   │   │   ├── bug_report.md
│   │   │   └── feature_request.md
│   │   └── workflows/
│   │       └── arduino-ci.yml             # GitHub Actions CI for Arduino CLI compilation
│   ├── src/
│   │   └── esp32_precision_trackpad/
│   │       └── esp32_precision_trackpad.ino # 🏆 Production Firmware (v1.0.0)
│   ├── test/
│   │   ├── ps2_diagnostics/               # Hardware bring-up diagnostic sketches
│   │   │   ├── esptrackpad_diagnostic/
│   │   │   ├── ps2_touchpad_test/
│   │   │   └── uno_diagnostic/
│   │   ├── i2c_experiments/               # The I2C / SMBus scanner trials
│   │   │   ├── i2c_touchpad_test/
│   │   │   ├── i2c_scanner_esp32/
│   │   │   └── i2c_scanner_uno/
│   │   └── legacy_mouse_emulation/        # Phase 2 BleMouse emulation sketch
│   │       └── ble_trackpad_mouse/
│   ├── docs/
│   │   ├── hardware_wiring.md             # Detailed pinout & pull-up guide
│   │   └── protocol_notes.md              # PS/2 vs I2C vs PTP technical notes
│   ├── res/
│   │   └── i2c_test_terminal_output.png   # Diagnostic terminal screenshot
│   ├── .gitignore                         # Arduino/ESP32 build artifact ignore rules
│   ├── CHANGELOG.md                       # Milestone log from bring-up to v1.0.0
│   ├── CODE_OF_CONDUCT.md
│   ├── CONTRIBUTING.md
│   ├── LICENSE                            # MIT License
│   └── README.md                          # Public GitHub README with badges & emojis
│
└── Esp32_ble_trackpad/                    # 📁 Original Workspace (Untouched & Preserved)
    ├── Bluetooth_trackpad_ps2_v1/
    │   └── Bluetooth_trackpad_ps2_v1.ino  # Source of v1.0.0 PTP firmware
    ├── ble_trackpad/                      # Phase 2 mouse emulation
    ├── ble_trackpad_backup_ps2/           # Phase 2 mouse emulation with tuned inertia
    ├── esptrackpad/                       # Initial ESP32 PS/2 diagnostic
    ├── ps2_touchpad_test/                 # Initial connection check
    ├── unotest/                           # Arduino Uno diagnostic & I2C scanner
    ├── i2c_touchpad_test/                 # ESP32 I2C HID experiment
    ├── i2c_scanner/                       # Dual-pin I2C scanner
    ├── i2c_scanner_esp32/                 # Standard ESP32 I2C scanner
    └── Screenshot 2026-08-07 230808.png  # I2C diagnostic capture
```

---

## 8. Configuration Parameters & Tuning Reference

All operational constants in `esp32_precision_trackpad.ino` / `Bluetooth_trackpad_ps2_v1.ino`:

| Parameter | Location | Value | Purpose & Tuning Guidance |
| :--- | :--- | :--- | :--- |
| `PS2_CLK_PIN` | Line 16 | `16` | ESP32 GPIO connected to Touchpad Clock (RX2 pin on silkscreen). |
| `PS2_DATA_PIN` | Line 17 | `17` | ESP32 GPIO connected to Touchpad Data (TX2 pin on silkscreen). |
| `LED_PIN` | Line 18 | `2` | Built-in blue LED for pairing status indication. |
| `PS2_BUF_SIZE` | Line 233 | `64` | Ring buffer size for the interrupt receiver. Must be a power of 2. |
| `FINGER_Z_OFF` | Line 356 | `12` | Minimum pressure threshold. Packets with $Z < 12$ are treated as finger lift. Increase if phantom touches occur when hovering; decrease if light touches drop. |
| Rate Limit Delay | Line 502 | `12` ms | Rate limits continuous BLE updates to ~80 Hz ($1000/12 \approx 83\,\text{Hz}$) to prevent BLE queue latency. |
| Coordinate Bounds | Descriptor | `0..8191` | Full 13-bit hardware coordinate resolution of the Synaptics sensor. |
| AGM Finger 3 Offset | Line 477 | `+200` | Synthesized X-axis offset for Finger 3 when $W=1$ (since hardware sends 2 coordinate pairs + multi-touch flag). |

---

## 9. Current Status, Limitations & Future Roadmap

### 9.1 Current Operational Status
* **Operating System:** Tested and fully operational on **Windows 10** and **Windows 11**.
* **Gestures Supported Out-of-the-Box:**
  * 1-finger cursor glide, tap-to-click, physical left/right buttons.
  * 2-finger kinetic inertial scrolling (vertical & horizontal).
  * 2-finger pinch-to-zoom.
  * 2-finger tap for context menu (right click).
  * 3-finger swipe up (Task View / Desktops).
  * 3-finger swipe left/right (Alt+Tab app switching).
  * 4-finger gestures (Notification Center, virtual desktops).

### 9.2 Known Limitations
1. **Third Finger Position is Synthetic:** Synaptics PS/2 Absolute Mode reports a maximum of two coordinate pairs ($X, Y$ and $X_2, Y_2$) plus a flag ($W=1$) indicating three or more fingers. The third finger position is currently synthesized as an offset ($X_2 + 200, Y_2$). This is sufficient to trigger Windows 3-finger swipes, but true 3-finger geometric tracking is limited by the PS/2 packet payload.
2. **Power Management:** The ESP32 currently runs in continuous active loop with the Bluetooth modem active (~80–120 mA). Deep sleep / light sleep with touchpad touch wake has not yet been implemented.
3. **Physical Packaging:** The prototype runs on breadboard/flying wires. A dedicated 3D-printed enclosure, integrated 1S LiPo battery, TP4056 charging module, and power switch are needed for a polished standalone peripheral.

### 9.3 Roadmap & Planned Enhancements
* [ ] **ESP32 Power Optimization:** Implement light sleep between packets and dynamic CPU clock scaling (80 MHz instead of 240 MHz) to reduce current consumption below 25 mA.
* [ ] **Capacitive Touch Wake:** Configure an ESP32 GPIO touch wake or PS/2 clock interrupt wake from deep sleep after 5 minutes of inactivity.
* [ ] **Linux / macOS Testing:** Validate descriptor compatibility with Linux `libinput` and macOS Bluetooth HID drivers.
* [ ] **Custom Enclosure:** Model a custom angled 3D-printable ergonomic housing with battery compartment.

---

## 10. Developer Guide: How to Compile & Flash

### Prerequisites
* **Arduino IDE:** Version 2.x recommended.
* **ESP32 Core:** Install `esp32` by Espressif Systems (v2.0.x or v3.0.x) via Boards Manager.
* **Libraries:** No external third-party libraries required! Uses standard ESP32 built-in BLE libraries:
  ```cpp
  #include <BLEDevice.h>
  #include <BLEUtils.h>
  #include <BLEServer.h>
  #include <BLEHIDDevice.h>
  ```

### Build Settings in Arduino IDE
* **Board:** `ESP32 Dev Module`
* **CPU Frequency:** `240MHz (WiFi/BT)`
* **Flash Frequency:** `80MHz`
* **Flash Mode:** `QIO`
* **Partition Scheme:** `Default 4MB with spiffs`
* **Core Debug Level:** `None` (or `Verbose` for serial diagnostics)
* **Upload Speed:** `921600`
