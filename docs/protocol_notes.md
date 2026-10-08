# Protocol Architecture: PS/2 vs. I2C vs. Windows PTP

## 1. Why PS/2 Absolute Mode Won Over I2C / SMBus
During bringup, we investigated whether the TM3096 could be driven over I2C/SMBus:
- An I2C scan detected address `0x2C` (Synaptics RMI4 address).
- However, reading input reports from the HID descriptor resulted in static zeros (`01 00 00 00`, `X=0 Y=0 Z=0`).
- Modern laptop I2C touchpads depend on ACPI host power management, GPIO reset handshakes, and proprietary vendor initialization registers.
- **PS/2 Absolute Mode (`0xC1`)**, by contrast, is self-contained, robust, and streams raw 6-byte absolute coordinates independently of any operating system drivers.

---

## 2. Solving Windows Latency & Stuttering Bugs

### Bug 1: BLE Preemption Packet Drops
- *Problem:* In polling mode, when the ESP32 FreeRTOS BLE stack took CPU cycles to manage radio advertising and packet notifications, the CPU missed the ~12 kHz falling clock edges on PS/2. A single dropped bit corrupted packet framing.
- *Solution:* An interrupt service routine (`attachInterrupt` on CLK falling edge) with `IRAM_ATTR` saves incoming bits into a volatile 64-byte circular buffer (`ps2Buf`). The ISR captures clock edges in microseconds without data loss.

### Bug 2: BLE Buffer Saturation & Lag
- *Problem:* Streaming every decoded packet over BLE flooded the notification buffer, causing a 200–400 ms rubber-band input lag.
- *Solution:* Implemented an ~80 Hz (12 ms) rate limiter for continuous tracking, while giving finger release events (`Tip=0`) and button clicks `urgent` priority so they flush immediately with zero delay.

### Bug 3: Multi-Touch AGM Coordinate Desync
- *Problem:* Synaptics sends multi-touch data in two sequential packets: an AGM packet ($W=2$) carrying Finger 2, followed by a primary packet ($W=0$ or $1$) carrying Finger 1. Sending reports on every raw packet caused Windows to receive alternating 1-finger and 2-finger frames, causing violent scroll judder.
- *Solution:* The firmware buffers the AGM packet ($W=2$) and only dispatches the unified PTP report when the primary packet arrives (`if (!gTouch.isAgm) sendPtpReport()`).

### Bug 4: Phantom Touches on Finger Lift
- *Problem:* Abruptly stopping transmission when pressure dropped ($Z < 12$) left Windows with no contact release event, assuming the finger was still held down.
- *Solution:* Implemented active contact lifecycle tracking (`f1_active`, `f2_active`, `f3_active`). When pressure drops below threshold, the firmware sends an explicit lift report with `Tip=0, Conf=0` for each active contact ID before going idle.
