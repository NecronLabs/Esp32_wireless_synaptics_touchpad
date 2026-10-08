# Hardware & Wiring Guide

This document details the physical wiring, electrical considerations, and troubleshooting steps for connecting the **Synaptics TM3096 (T1321B)** touchpad to an **ESP32**.

---

## The Hardware: Synaptics TM3096 (T1321B IC)
The touchpad was salvaged from an older Dell laptop. On the underside, it features the Synaptics **T1321B** ASIC.

```
Touchpad Connector: 8-pin FPC (0.5mm / 1.0mm pitch depending on revision)
Pin 4: Verified GND (continuity to copper shield / ground plane)
Pin 2/3/Test pads: VCC (3.3V), CLK, DATA
```

---

## Schematic / Wiring

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

---

## Electrical Rules & Gotchas

### 1. No Series Resistors on VCC
During initial prototyping, a 1kΩ resistor was placed in series with VCC to protect the board.
* **Result:** The touchpad drew 20–40 mA, which dropped 20V–40V across the 1kΩ resistor. The chip received near 0V and remained dead.
* **Rule:** Connect the ESP32's 3.3V regulator pin directly to the touchpad VCC pad.

### 2. Pull-up Resistor Value: Why 10kΩ?
PS/2 lines are open-collector. Both device and microcontroller only pull lines LOW; external resistors pull them HIGH to 3.3V.
* **Why not 1kΩ?** Pulling a 1kΩ line low requires sinking 3.3 mA per line. The internal open-drain FETs on the Synaptics chip are tiny and can overheat, plus it wastes significant battery.
* **Why 10kΩ?** At PS/2 clock speeds (~12 kHz), the RC rise time with 10kΩ is ~1 µs, which is well within the 60 µs clock period. It provides crisp digital signals with minimal power draw.

### 3. Pin Mapping via Multimeter
If working with a bare board or test pads:
1. **Find GND:** Set multimeter to continuity mode. Touch one probe to the large metal backing/ground plane. Probe pads until you hear a beep. On our connector, only **Pin 4** beeped.
2. **Find Signal Lines:** With the touchpad unpowered, signal lines will show a ~0.5V–0.7V forward diode drop to GND (ESD diodes). VCC shows open/charging capacitor behavior.
