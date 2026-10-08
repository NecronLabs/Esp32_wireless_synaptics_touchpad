# Protocol Architecture: PS/2 vs. I2C vs. Windows PTP

## 1. Why PS/2 Absolute Mode Won Over I2C / SMBus
During experimentation, we tested whether the TM3096 could be driven over I2C/SMBus.
- An I2C scan successfully detected address `0x2C` (Synaptics RMI4 address).
- However, reading input reports from the HID descriptor resulted in static zeros (`01 00 00 00`, `X=0 Y=0 Z=0`).
- **Reason:** In modern laptops, Synaptics touchpads rely on ACPI power states, host-issued GPIO reset pulses, and custom register configuration sequences that are complex to reverse-engineer on a microcontroller.
- **PS/2 Absolute Mode (`0xC1`)**, by contrast, is fully self-contained. It is activated via standard PS/2 sliced commands (`0xE8` nibble sequence) and streams raw 6-byte packets synchronously.

---

## 2. Synaptics Absolute Packet Structure (6 Bytes)

When configured with mode `0xC1`, the touchpad sends 6-byte packets:
```
Byte 0: [ 1  0  W3 W2   1  W1  R   L  ]
Byte 1: [ Y11 Y10 Y9 Y8 X11 X10 X9 X8 ]
Byte 2: [ Z7  Z6  Z5 Z4  Z3  Z2 Z1 Z0 ]  (Pressure)
Byte 3: [ 1  1  Y12 X12 0  W0  R^L M  ]
Byte 4: [ X7  X6  X5 X4  X3  X2 X1 X0 ]
Byte 5: [ Y7  Y6  Y5 Y4  Y3  Y2 Y1 Y0 ]
```

### Multi-Touch / AGM (Advanced Gesture Mode)
The $W$ value indicates finger count and packet type:
* **$W = 2$:** AGM packet. The coordinates encode the **Secondary Finger** ($X_2, Y_2, Z_2$).
* **$W = 0$:** Indicates **2 fingers** on pad.
* **$W = 1$:** Indicates **3 fingers** on pad.
* **$W \ge 4$:** Indicates **1 finger** on pad ($W$ represents finger width).
* **$Z < 12$:** Finger lifted.

---

## 3. Windows Precision Touchpad (PTP) HID Translation

Instead of emulating a mouse (relative deltas), the ESP32 acts as a **Digitizer Touchpad** (`Usage Page 0x0D`, `Usage 0x05`).

### Coordinate Mapping
- Synaptics coordinates: $(0, 0)$ is bottom-left. Range: $0 \dots 8191$.
- Windows PTP coordinates: $(0, 0)$ is top-left. Range: $0 \dots 8191$.
- Inversion applied:
  ```cpp
  mappedY = 8191 - touchY;
  ```

### Contact Reporting
For each frame, the ESP32 sends:
1. **Finger 1:** Contact ID 1, Tip Switch (1 = touching, 0 = released), Confidence bit, $X$, $Y$.
2. **Finger 2:** Contact ID 2, Tip Switch, Confidence bit, $X_2$, $Y_2$.
3. **Finger 3:** Contact ID 3, Tip Switch, Confidence bit, $X_3$, $Y_3$.
4. **Scan Time:** Relative 100 µs timestamp.
5. **Contact Count:** Number of active contacts.
6. **Buttons:** Physical Left / Right button bits.

Because Windows receives a genuine PTP report, Windows' built-in driver handles:
- Inertial scrolling
- Pinch-to-zoom
- 3-finger swipe (Task View / virtual desktops)
- Palm rejection
