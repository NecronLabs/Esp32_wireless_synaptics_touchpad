# Hardware & Wiring Guide

This document details the physical wiring, electrical considerations, and troubleshooting steps for connecting the **Synaptics TM3096 (T1321B)** touchpad to an **ESP32**.

---

## 1. The Hardware: Synaptics TM3096 (T1321B IC)
The touchpad was salvaged from an older Dell laptop. On the underside, it features the Synaptics **T1321B** ASIC.

```
Touchpad Connector: 8-pin FPC
Pin 4: Verified GND (continuity to copper shield / ground plane)
Signal/Power: Direct test pads on underside & connector
```

---

## 2. Reverse-Engineering Pinout & Power

### Locating Ground
- Online pinouts claimed Pin 1 was GND and Pin 2 was VCC. Probing proved this false.
- Multimeter continuity testing against the large exposed ground plane and metal chassis confirmed **only Pin 4 connects to GND**.

### Locating Signal Lines (Diode Test Method)
- With the board unpowered, set your multimeter to Diode Check (red probe on GND, black probe on unknown pins).
- The CLK and DATA lines produce forward diode drops (~0.5V to 0.7V) due to internal CMOS ESD clamp diodes.
- VCC lines show open circuit or a slow capacitive charging curve.

### Safe Operating Voltage
- Laptop trackpads can be 5V or 3.3V. Applying 5V to a 3.3V ASIC will permanently burn the IC.
- Probing decoupling capacitors and tracing the power plane to the input of the T1321B confirmed the chip runs natively on **3.3V**. Power is supplied directly from the ESP32's onboard 3.3V regulator rail.

---

## 3. Pull-Up Resistor Network (Eliminating Bus Noise)

PS/2 is an open-collector (open-drain) bus. The touchpad and microcontroller only pull lines LOW; external resistors must pull lines HIGH to 3.3V.

```
ESP32 GPIO 16 (CLK)  ──┬──[10kΩ]── 3.3V
                       └── Touchpad CLK

ESP32 GPIO 17 (DATA) ──┬──[10kΩ]── 3.3V
                       └── Touchpad DATA
```

* **Why external pull-ups are essential:** Without them, floating lines pick up capacitive coupling and EMI noise across jumper wires, resulting in phantom clock edges, framing errors, and parity mismatch errors.
* **Why the internal pull-up is insufficient:** ESP32's internal `INPUT_PULLUP` is ~45kΩ. With breadboard parasitic capacitance (~50–100 pF), the RC rise time exceeds 15 µs, deforming clock edges.
* **Why 10kΩ over 1kΩ:** 1kΩ pulls 3.3 mA per line through the tiny open-drain transistors on the Synaptics ASIC, creating unnecessary heat and battery drain. 10kΩ provides clean ~1 µs rise times at the ~12 kHz clock frequency with minimal power draw.
