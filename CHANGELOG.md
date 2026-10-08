# Changelog

All notable changes and milestones in this project are documented here.

## [v1.0.0] - 2026-08-08 (Current Latest Version)
### Added
- **Native Windows Precision Touchpad (PTP) implementation** over BLE HID.
- Custom USB/BLE HID Digitizer Descriptor with 3-finger contact collections, tip switch, confidence flag, contact IDs, and 16-bit coordinates (0–8191).
- Inverted Y-axis mapping (`8191 - Y`) so Synaptics coordinates match Windows screen coordinates.
- Synaptics Advanced Gesture Mode (AGM) decoding (`W=2` secondary finger coordinate extraction interleaved with primary packets).
- Interrupt-driven PS/2 decoding (`ps2ClkISR` with 64-byte circular buffer) for zero dropped packets during BLE transmission.
- Smart BLE output rate limiting (~80 Hz / 12 ms) with zero-latency priority flush on finger release or button state change.
- Status LED on GPIO 2 (blinks when disconnected, solid when paired).

## [v0.2.0] - 2026-08-08
### Added
- BLE Mouse emulation using `BleMouse` library (`test/legacy_mouse_emulation/ble_trackpad_mouse/`).
- Software-based gesture engine: 1-finger move, 1-finger tap click, 2-finger scroll, 2-finger tap right click, inertia momentum glide.
### Superseded By
- v1.0.0 (Native PTP provides smoother scrolling and native OS multi-touch gestures without software emulation).

## [v0.1.5] - 2026-08-07
### Explored
- I2C / SMBus communication tests on Synaptics TM3096 (`test/i2c_experiments/`).
- Discovered address `0x2C`, but HID-over-I2C yielded empty reports without host ACPI initialization.
- Decided to stick with PS/2 Absolute Mode for reliability and independence.

## [v0.1.0] - 2026-08-07
### Added
- Initial hardware bring-up and diagnostic sketches (`test/ps2_diagnostics/`).
- Resolved electrical issues: removed 1kΩ series resistor from VCC, installed 10kΩ pull-ups, and mapped true GND (Pin 4).
- Verified PS/2 basic reset response (`0xFA 0xAA 0x00`).
