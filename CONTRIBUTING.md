# Contributing to ESP32 BLE Precision Trackpad

Contributions, bug reports, and hardware testing across other touchpad models are very welcome!

## Ways to Contribute
1. **Testing other Synaptics models**: If you have a different Synaptics chip (e.g., T1004, TM2xxx) or trackpad model, test the pinout or packet format and submit your findings or pinout diagrams.
2. **Linux & macOS compatibility testing**: The current PTP HID descriptor is targeted for Microsoft Windows Precision Touchpad specification. Testing on modern Linux (libinput) or macOS is highly appreciated.
3. **Firmware optimizations**: Lowering BLE transmission latency, battery optimization, or sleep modes.

## Development Workflow
1. Fork the repository.
2. Create your feature branch (`git checkout -b feature/cool-idea`).
3. Test your code on actual hardware before opening a pull request.
4. Keep Arduino sketches self-contained and clean.
5. Submit a Pull Request with a clear description of hardware used and test results.
