// ============================================================
//  Synaptics TM3096 (T1321B) Touchpad — PS/2 Diagnostic Sketch
//  Board  : Arduino Uno (ATmega328P)
//  CLK    : Pin 3  (INT1 — interrupt-capable for future use)
//  DATA   : Pin 5
//
//  WIRING:
//    Touchpad VCC  → Arduino 5V
//    Touchpad GND  → Arduino GND
//    Touchpad CLK  → Pin 3  + 4.7kΩ or 10kΩ pull-up to 5V
//    Touchpad DATA → Pin 5  + 4.7kΩ or 10kΩ pull-up to 5V
//
//  This sketch automatically tries swapping CLK/DATA if the
//  first attempt fails, and includes a raw bus activity monitor
//  to detect any signal transitions.
//
//  Open Serial Monitor at 115200 baud to see results.
// ============================================================

// These may get swapped automatically during diagnostics
uint8_t PS2_CLK_PIN  = 3;    // Interrupt-capable pin (INT1)
uint8_t PS2_DATA_PIN = 5;

// ---------- timing ----------
#define PS2_TIMEOUT_US  100000UL   // 100 ms timeout for clock edges
#define PS2_BIT_DELAY   50         // µs pause between bit operations

// ============================================================
//  Low-level PS/2 helpers  (open-collector emulation)
// ============================================================

// Release a pin — set as INPUT, external pull-up brings it HIGH
static inline void ps2Release(uint8_t pin) {
  pinMode(pin, INPUT);
}

// Drive a pin LOW
static inline void ps2Low(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

// Wait for CLK to reach desired level. Returns false on timeout.
static bool ps2WaitClk(uint8_t level) {
  unsigned long t0 = micros();
  while (digitalRead(PS2_CLK_PIN) != level) {
    if ((micros() - t0) > PS2_TIMEOUT_US) return false;
  }
  return true;
}

// ============================================================
//  Read one byte from device (device-to-host)
//  Returns  0..255 on success
//          -1  timeout
//          -2  bad start bit
//          -3  bad stop bit
//          -4  parity error
// ============================================================
int ps2ReadByte() {
  uint8_t data   = 0;
  uint8_t parity = 1;

  // Both lines released so device can drive them
  ps2Release(PS2_CLK_PIN);
  ps2Release(PS2_DATA_PIN);

  // --- Start bit (must be 0) ---
  if (!ps2WaitClk(LOW))  return -1;
  if (digitalRead(PS2_DATA_PIN) != LOW) return -2;
  if (!ps2WaitClk(HIGH)) return -1;

  // --- 8 data bits (LSB first) ---
  for (uint8_t i = 0; i < 8; i++) {
    if (!ps2WaitClk(LOW))  return -1;
    uint8_t bit = digitalRead(PS2_DATA_PIN);
    data |= (bit << i);
    parity ^= bit;
    if (!ps2WaitClk(HIGH)) return -1;
  }

  // --- Parity bit ---
  if (!ps2WaitClk(LOW))  return -1;
  uint8_t pBit = digitalRead(PS2_DATA_PIN);
  parity ^= pBit;
  if (!ps2WaitClk(HIGH)) return -1;

  // --- Stop bit (must be 1) ---
  if (!ps2WaitClk(LOW))  return -1;
  if (digitalRead(PS2_DATA_PIN) != HIGH) return -3;
  if (!ps2WaitClk(HIGH)) return -1;

  if (parity != 0) return -4;

  return (int)data;
}

// ============================================================
//  Send one byte to device (host-to-device)
//  Returns true on successful ACK.
// ============================================================
bool ps2SendByte(uint8_t data) {
  uint8_t parity = 1;

  // 1. Inhibit — pull CLK low >= 100 µs
  ps2Low(PS2_CLK_PIN);
  delayMicroseconds(150);

  // 2. Pull DATA low (start bit), then release CLK
  ps2Low(PS2_DATA_PIN);
  delayMicroseconds(10);
  ps2Release(PS2_CLK_PIN);

  // 3. Clock out 8 data bits on falling CLK edges
  for (uint8_t i = 0; i < 8; i++) {
    if (!ps2WaitClk(LOW)) return false;
    uint8_t bit = (data >> i) & 1;
    parity ^= bit;
    if (bit) ps2Release(PS2_DATA_PIN);
    else     ps2Low(PS2_DATA_PIN);
    delayMicroseconds(PS2_BIT_DELAY);
    if (!ps2WaitClk(HIGH)) return false;
  }

  // 4. Parity bit
  if (!ps2WaitClk(LOW)) return false;
  if (parity) ps2Release(PS2_DATA_PIN);
  else        ps2Low(PS2_DATA_PIN);
  delayMicroseconds(PS2_BIT_DELAY);
  if (!ps2WaitClk(HIGH)) return false;

  // 5. Stop bit — release DATA
  if (!ps2WaitClk(LOW)) return false;
  ps2Release(PS2_DATA_PIN);
  delayMicroseconds(PS2_BIT_DELAY);
  if (!ps2WaitClk(HIGH)) return false;

  // 6. Device ACK — device pulls DATA low
  if (!ps2WaitClk(LOW)) return false;
  if (digitalRead(PS2_DATA_PIN) != LOW) return false;
  if (!ps2WaitClk(HIGH)) return false;

  // 7. Done — release bus
  ps2Release(PS2_CLK_PIN);
  ps2Release(PS2_DATA_PIN);
  return true;
}

// ============================================================
//  Pretty-print a read result (hex + decimal)
// ============================================================
void printRead(const char* label, int val) {
  Serial.print(F("  "));
  Serial.print(label);
  Serial.print(F(": "));
  if (val >= 0) {
    Serial.print(F("0x"));
    if (val < 0x10) Serial.print('0');
    Serial.print(val, HEX);
    Serial.print(F(" ("));
    Serial.print(val, DEC);
    Serial.println(')');
  } else {
    Serial.print(F("ERROR "));
    Serial.println(val);
  }
}

// ============================================================
//  Send a PS/2 command and read its ACK byte
//  Returns ACK value (0xFA expected), or -1 on failure.
// ============================================================
int ps2Command(uint8_t cmd) {
  if (!ps2SendByte(cmd)) return -1;
  delay(20);
  return ps2ReadByte();
}

// ============================================================
//  Synaptics Extended Identify
//  Magic knock: SetSampleRate(200,100,80) → GetDeviceID
//  Synaptics pads reply with vendor-specific ID bytes.
// ============================================================
void synapticsIdentify() {
  Serial.println(F("\n--- Synaptics Extended Identify ---"));

  uint8_t magic[] = {200, 100, 80};
  for (uint8_t i = 0; i < 3; i++) {
    int ack = ps2Command(0xF3);            // Set Sample Rate
    if (ack != 0xFA) {
      Serial.print(F("  SetSampleRate cmd failed, ACK="));
      Serial.println(ack);
      return;
    }
    ack = ps2Command(magic[i]);            // rate value
    if (ack != 0xFA) {
      Serial.print(F("  Rate value failed, ACK="));
      Serial.println(ack);
      return;
    }
  }

  // GetDeviceID — Synaptics returns ACK + 2 ID bytes
  int ack = ps2Command(0xF2);
  int id1 = ps2ReadByte();
  int id2 = ps2ReadByte();

  printRead("GetDevID ACK", ack);
  printRead("ID byte 1   ", id1);
  printRead("ID byte 2   ", id2);

  if (id1 == 0x47 || id1 == 0x46) {
    Serial.println(F("  >> Synaptics touchpad CONFIRMED!"));
  } else {
    Serial.println(F("  >> ID doesn't match known Synaptics value"));
  }
}

// ============================================================
//  Try a full PS/2 RESET on current pin assignment.
//  Returns true if the pad responded.
// ============================================================
bool tryPS2Reset() {
  ps2Release(PS2_CLK_PIN);
  ps2Release(PS2_DATA_PIN);
  delay(200);

  // Check bus idle
  bool clkOk = (digitalRead(PS2_CLK_PIN) == HIGH);
  bool datOk = (digitalRead(PS2_DATA_PIN) == HIGH);
  Serial.print(F("    Pin "));
  Serial.print(PS2_CLK_PIN);
  Serial.print(F(" (CLK): "));
  Serial.println(clkOk ? F("HIGH") : F("LOW"));
  Serial.print(F("    Pin "));
  Serial.print(PS2_DATA_PIN);
  Serial.print(F(" (DATA): "));
  Serial.println(datOk ? F("HIGH") : F("LOW"));

  // Send RESET
  bool sent = ps2SendByte(0xFF);
  Serial.print(F("    RESET send: "));
  Serial.println(sent ? F("OK") : F("FAILED"));
  return sent;
}

// ============================================================
//  Monitor raw pin activity for `durMs` milliseconds.
//  Counts transitions (edges) on both pins.
// ============================================================
void monitorBusActivity(unsigned long durMs) {
  Serial.print(F("  Monitoring raw activity for "));
  Serial.print(durMs / 1000);
  Serial.println(F(" seconds..."));
  Serial.println(F("  (Touch the pad during this time!)"));

  uint8_t pin3state = digitalRead(3);
  uint8_t pin5state = digitalRead(5);
  unsigned long edges3 = 0;
  unsigned long edges5 = 0;

  unsigned long t0 = millis();
  while (millis() - t0 < durMs) {
    uint8_t r3 = digitalRead(3);
    uint8_t r5 = digitalRead(5);
    if (r3 != pin3state) { edges3++; pin3state = r3; }
    if (r5 != pin5state) { edges5++; pin5state = r5; }
  }

  Serial.print(F("  Pin 3 transitions: "));
  Serial.println(edges3);
  Serial.print(F("  Pin 5 transitions: "));
  Serial.println(edges5);

  if (edges3 == 0 && edges5 == 0) {
    Serial.println(F("\n  NO ACTIVITY on either pin!"));
    Serial.println(F("  This means:"));
    Serial.println(F("  -> Touchpad is NOT using these pins for PS/2"));
    Serial.println(F("  -> OR the touchpad is not powered on"));
    Serial.println(F("  -> OR it uses I2C/SMBus (try the I2C scanner sketch)"));
  } else {
    Serial.print(F("\n  Activity detected! "));
    if (edges3 > edges5) {
      Serial.println(F("Pin 3 is more active (likely CLK)"));
    } else if (edges5 > edges3) {
      Serial.println(F("Pin 5 is more active (likely CLK)"));
    } else {
      Serial.println(F("Both pins show similar activity"));
    }
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println(F("============================================="));
  Serial.println(F("  Synaptics TM3096 (T1321B) PS/2 Test v2"));
  Serial.println(F("  Board : Arduino Uno"));
  Serial.println(F("  Pins  : 3 and 5 (auto-swap if needed)"));
  Serial.println(F("  Pull-ups : 10k to 5V on both lines"));
  Serial.println(F("============================================="));
  Serial.println();

  // Start with both lines released
  ps2Release(3);
  ps2Release(5);
  delay(500);   // let touchpad boot

  // -------- Step 1: Bus idle check --------
  Serial.println(F("[1] Bus Idle Check"));
  bool p3hi = (digitalRead(3) == HIGH);
  bool p5hi = (digitalRead(5) == HIGH);
  Serial.print(F("  Pin 3: "));
  Serial.println(p3hi ? F("HIGH  OK") : F("LOW   FAIL"));
  Serial.print(F("  Pin 5: "));
  Serial.println(p5hi ? F("HIGH  OK") : F("LOW   FAIL"));

  if (!p3hi && !p5hi) {
    Serial.println(F("\n  BOTH PINS LOW! Check:"));
    Serial.println(F("  -> Are pull-up resistors connected to 5V?"));
    Serial.println(F("  -> Is VCC connected to the touchpad?"));
    Serial.println(F("  -> Is GND connected?"));
    Serial.println(F("  Cannot continue without bus idle state.\n"));
  }

  // -------- Step 2: Try to read spontaneous BAT --------
  Serial.println(F("\n[2] Checking for spontaneous BAT..."));
  delay(300);
  int bat1 = ps2ReadByte();
  if (bat1 >= 0) {
    printRead("Spontaneous byte 1 (0xAA=BAT OK)", bat1);
    int bat2 = ps2ReadByte();
    if (bat2 >= 0) printRead("Spontaneous byte 2 (0x00=Mouse ID)", bat2);
  } else {
    Serial.println(F("  None received (normal)"));
  }

  // -------- Step 3: Send RESET with auto-swap --------
  Serial.println(F("\n[3] Sending RESET (0xFF) — Attempt 1"));
  Serial.print(F("  CLK=Pin "));
  Serial.print(PS2_CLK_PIN);
  Serial.print(F("  DATA=Pin "));
  Serial.println(PS2_DATA_PIN);

  bool sent = tryPS2Reset();

  if (!sent) {
    // Auto-swap CLK and DATA pins and retry
    Serial.println(F("\n[3b] RESET failed. Auto-swapping CLK<->DATA..."));
    uint8_t tmp = PS2_CLK_PIN;
    PS2_CLK_PIN = PS2_DATA_PIN;
    PS2_DATA_PIN = tmp;
    Serial.print(F("  Now: CLK=Pin "));
    Serial.print(PS2_CLK_PIN);
    Serial.print(F("  DATA=Pin "));
    Serial.println(PS2_DATA_PIN);

    delay(200);
    sent = tryPS2Reset();

    if (!sent) {
      Serial.println(F("\n  BOTH orientations failed!"));
      Serial.println(F("  This touchpad likely does NOT use PS/2."));
    }
  }

  // -------- Step 4: Read reset response --------
  if (sent) {
    Serial.println(F("\n[4] Reset Response (expect: 0xFA, 0xAA, 0x00)"));
    delay(600);

    int r1 = ps2ReadByte();
    int r2 = ps2ReadByte();
    int r3 = ps2ReadByte();

    printRead("Byte 1 (ACK  0xFA)", r1);
    printRead("Byte 2 (BAT  0xAA)", r2);
    printRead("Byte 3 (ID   0x00)", r3);

    if (r1 == 0xFA && r2 == 0xAA && r3 == 0x00) {
      Serial.println(F("\n  TOUCHPAD ALIVE! Reset successful."));
    } else if (r1 == 0xFA && r2 == 0xAA) {
      Serial.println(F("\n  Partial success: ACK+BAT OK, ID byte unexpected"));
    } else if (r1 == 0xFA) {
      Serial.println(F("\n  Got ACK but BAT failed or timed out"));
    } else {
      Serial.println(F("\n  Unexpected response. Wiring may still be wrong."));
    }
  }

  // -------- Step 5: If RESET worked, continue with identification --------
  if (sent) {
    Serial.println(F("\n[5] Standard GetDeviceID (0xF2)"));
    delay(100);
    int ack = ps2Command(0xF2);
    printRead("ACK (want 0xFA)", ack);
    if (ack == 0xFA) {
      int devId = ps2ReadByte();
      printRead("Device ID     ", devId);
      if (devId == 0x00)
        Serial.println(F("  >> Standard PS/2 mouse/touchpad"));
      else if (devId == 0x03)
        Serial.println(F("  >> IntelliMouse (scroll wheel)"));
    }

    // Synaptics Extended Identify
    synapticsIdentify();

    // Set Sample Rate & Resolution
    Serial.println(F("\n[6] Setting sample rate to 100/sec"));
    int a1 = ps2Command(0xF3);
    int a2 = ps2Command(100);
    Serial.print(F("  Result: "));
    Serial.println((a1 == 0xFA && a2 == 0xFA) ? F("OK") : F("FAILED"));

    Serial.println(F("    Setting resolution to 4 counts/mm"));
    a1 = ps2Command(0xE8);
    a2 = ps2Command(0x02);
    Serial.print(F("  Result: "));
    Serial.println((a1 == 0xFA && a2 == 0xFA) ? F("OK") : F("FAILED"));

    // Enable Data Reporting
    Serial.println(F("\n[7] Enable Data Reporting (0xF4)"));
    delay(50);
    int ack2 = ps2Command(0xF4);
    printRead("ACK (want 0xFA)", ack2);
    if (ack2 == 0xFA) {
      Serial.println(F("  >> Reporting ENABLED. Touch the pad!"));
    } else {
      Serial.println(F("  >> Enable failed. Trying anyway..."));
    }
  }

  // -------- Step 8: Raw bus activity monitor (always runs) --------
  if (!sent) {
    Serial.println(F("\n[8] Raw Bus Activity Monitor"));
    Serial.println(F("  Since PS/2 failed, let's check if the pad"));
    Serial.println(F("  generates ANY electrical signals at all."));
    monitorBusActivity(5000);   // 5 second monitor

    Serial.println(F("\n============================================="));
    Serial.println(F("  PS/2 communication FAILED on both pin"));
    Serial.println(F("  orientations. Next steps:"));
    Serial.println(F("  "));
    Serial.println(F("  1. Try the I2C scanner sketch:"));
    Serial.println(F("     Wire SDA->A4, SCL->A5 and upload"));
    Serial.println(F("     unotest/i2c_scanner_uno.ino"));
    Serial.println(F("  "));
    Serial.println(F("  2. Try powering at 3.3V instead of 5V"));
    Serial.println(F("     (some T1321B pads are 3.3V only)"));
    Serial.println(F("  "));
    Serial.println(F("  3. Check the connector pinout again —"));
    Serial.println(F("     make sure CLK/DATA are correct"));
    Serial.println(F("  "));
    Serial.println(F("  4. Some pads have a mode-select pin that"));
    Serial.println(F("     must be tied HIGH or LOW for PS/2"));
    Serial.println(F("============================================="));
  }

  // Print listening header
  if (sent) {
    Serial.println();
    Serial.println(F("============================================="));
    Serial.println(F("  Listening for touchpad data packets..."));
    Serial.println(F("  Touch or slide your finger on the pad."));
    Serial.println(F("============================================="));
    Serial.println();
    Serial.println(F("  RAW(hex)           | dX    dY   | Btn"));
    Serial.println(F("  -------------------+------------+--------"));
  } else {
    Serial.println(F("\n  Entering raw monitor mode (watching pins)..."));
  }
}

// ============================================================
//  Packet counter for periodic status
// ============================================================
static unsigned long pktCount   = 0;
static unsigned long errCount   = 0;
static unsigned long lastReport = 0;

// ============================================================
//  LOOP — read & display standard PS/2 3-byte mouse packets
// ============================================================
void loop() {
  int b0 = ps2ReadByte();
  if (b0 < 0) {
    // No data — print a heartbeat every 5 seconds so user
    // knows the board hasn't frozen
    if (millis() - lastReport > 5000) {
      lastReport = millis();
      Serial.print(F("  ... waiting (pkts="));
      Serial.print(pktCount);
      Serial.print(F(" errs="));
      Serial.print(errCount);
      Serial.println(')');
    }
    return;
  }

  // Validate byte 0: bit 3 must always be 1 in a valid PS/2 packet
  if (!(b0 & 0x08)) {
    // Out-of-sync — try to resync by discarding
    errCount++;
    Serial.print(F("  SYNC ERR byte0=0x"));
    if (b0 < 0x10) Serial.print('0');
    Serial.println(b0, HEX);
    return;
  }

  int b1 = ps2ReadByte();
  int b2 = ps2ReadByte();

  if (b1 < 0 || b2 < 0) {
    errCount++;
    Serial.print(F("  PARTIAL PKT b0=0x"));
    Serial.println(b0, HEX);
    return;
  }

  pktCount++;

  // Decode standard 3-byte PS/2 packet
  //  Byte 0: [YOvf XOvf Ysign Xsign 1 MBtn RBtn LBtn]
  //  Byte 1: X movement (signed via Xsign)
  //  Byte 2: Y movement (signed via Ysign)

  bool lb   = b0 & 0x01;
  bool rb   = b0 & 0x02;
  bool mb   = b0 & 0x04;
  bool xOvf = b0 & 0x40;
  bool yOvf = b0 & 0x80;

  // Construct signed 9-bit values
  int dx = b1;
  int dy = b2;
  if (b0 & 0x10) dx -= 256;   // X sign bit
  if (b0 & 0x20) dy -= 256;   // Y sign bit

  // Print raw hex
  Serial.print(F("  0x"));
  if (b0 < 0x10) Serial.print('0');
  Serial.print(b0, HEX);
  Serial.print(F(" 0x"));
  if (b1 < 0x10) Serial.print('0');
  Serial.print(b1, HEX);
  Serial.print(F(" 0x"));
  if (b2 < 0x10) Serial.print('0');
  Serial.print(b2, HEX);

  // Print decoded movement
  Serial.print(F(" | dX="));
  if (dx >= 0) Serial.print('+');
  Serial.print(dx);
  Serial.print(F("\tdY="));
  if (dy >= 0) Serial.print('+');
  Serial.print(dy);

  // Print buttons
  Serial.print(F("\t| L="));
  Serial.print(lb ? 1 : 0);
  Serial.print(F(" R="));
  Serial.print(rb ? 1 : 0);
  Serial.print(F(" M="));
  Serial.print(mb ? 1 : 0);

  // Overflow flags
  if (xOvf || yOvf) {
    Serial.print(F("  OVF!"));
  }

  Serial.println();

  // Periodic stats every 200 packets
  if (pktCount % 200 == 0) {
    Serial.print(F("  --- Stats: "));
    Serial.print(pktCount);
    Serial.print(F(" packets, "));
    Serial.print(errCount);
    Serial.println(F(" errors ---"));
  }
}
