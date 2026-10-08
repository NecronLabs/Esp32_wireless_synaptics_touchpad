// ============================================================
//  Synaptics T1321B Touchpad — PS/2 Diagnostic Sketch
//  Board  : ESP32 (any variant)
//  CLK    : GPIO16  (RX2 pin of ESP32)  ← corrected after pin swap
//  DATA   : GPIO17  (TX2 pin of ESP32)  ← corrected after pin swap
//
//  REQUIRED HARDWARE:
//    - 10kΩ pull-up resistor on CLK  → 3.3 V
//    - 10kΩ pull-up resistor on DATA → 3.3 V
//
//  Open Serial Monitor at 115200 baud to see results.
// ============================================================

#define PS2_CLK_PIN   16   // RX2 — actual CLK after wiring correction
#define PS2_DATA_PIN  17   // TX2 — actual DATA after wiring correction

// ---------- timing helpers ----------
#define PS2_TIMEOUT_US  100000UL   // 100 ms timeout for clock edges
#define PS2_BIT_DELAY_US  50       // small delay between bit ops

// ============================================================
//  Low-level PS/2 helpers
// ============================================================

// Release a pin (open-collector HIGH — let pull-up do the work)
inline void ps2_release(uint8_t pin) {
  pinMode(pin, INPUT);
}

// Drive a pin LOW
inline void ps2_low(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

// Wait for CLK to reach `level`; returns false on timeout
bool ps2_wait_clk(uint8_t level) {
  unsigned long t = micros();
  while (digitalRead(PS2_CLK_PIN) != level) {
    if ((micros() - t) > PS2_TIMEOUT_US) return false;
  }
  return true;
}

// ============================================================
//  Read one byte from device (device-to-host)
//  Returns -1 on timeout or framing error.
// ============================================================
int ps2_read_byte() {
  uint8_t data = 0;
  uint8_t parity = 1;

  // Release both lines so device can drive them
  ps2_release(PS2_CLK_PIN);
  ps2_release(PS2_DATA_PIN);

  // --- Start bit (should be 0) ---
  if (!ps2_wait_clk(LOW))  return -1;   // wait CLK fall
  if (digitalRead(PS2_DATA_PIN) != LOW) return -2;  // start bit must be 0
  if (!ps2_wait_clk(HIGH)) return -1;   // wait CLK rise

  // --- 8 data bits (LSB first) ---
  for (int i = 0; i < 8; i++) {
    if (!ps2_wait_clk(LOW))  return -1;
    uint8_t bit = digitalRead(PS2_DATA_PIN);
    data |= (bit << i);
    parity ^= bit;
    if (!ps2_wait_clk(HIGH)) return -1;
  }

  // --- Parity bit ---
  if (!ps2_wait_clk(LOW))  return -1;
  uint8_t parityBit = digitalRead(PS2_DATA_PIN);
  parity ^= parityBit;
  if (!ps2_wait_clk(HIGH)) return -1;

  // --- Stop bit (should be 1) ---
  if (!ps2_wait_clk(LOW))  return -1;
  if (digitalRead(PS2_DATA_PIN) != HIGH) return -3;  // stop bit must be 1
  if (!ps2_wait_clk(HIGH)) return -1;

  if (parity != 0) return -4;  // parity mismatch

  return (int)data;
}

// ============================================================
//  Send one byte to device (host-to-device)
//  Returns false on timeout / ack error.
// ============================================================
bool ps2_send_byte(uint8_t data) {
  uint8_t parity = 1;

  // 1. Inhibit clock: pull CLK low for >= 100 µs
  ps2_low(PS2_CLK_PIN);
  delayMicroseconds(150);

  // 2. Pull DATA low (start bit), then release CLK
  ps2_low(PS2_DATA_PIN);
  delayMicroseconds(10);
  ps2_release(PS2_CLK_PIN);

  // 3. Send 8 data bits on falling CLK edges
  for (int i = 0; i < 8; i++) {
    if (!ps2_wait_clk(LOW)) return false;
    uint8_t bit = (data >> i) & 1;
    parity ^= bit;
    if (bit) ps2_release(PS2_DATA_PIN);
    else     ps2_low(PS2_DATA_PIN);
    delayMicroseconds(PS2_BIT_DELAY_US);
    if (!ps2_wait_clk(HIGH)) return false;
  }

  // 4. Parity bit
  if (!ps2_wait_clk(LOW)) return false;
  if (parity) ps2_release(PS2_DATA_PIN);
  else        ps2_low(PS2_DATA_PIN);
  delayMicroseconds(PS2_BIT_DELAY_US);
  if (!ps2_wait_clk(HIGH)) return false;

  // 5. Stop bit — release DATA (HIGH)
  if (!ps2_wait_clk(LOW)) return false;
  ps2_release(PS2_DATA_PIN);
  delayMicroseconds(PS2_BIT_DELAY_US);
  if (!ps2_wait_clk(HIGH)) return false;

  // 6. Wait for device ACK (DATA pulled low by device)
  if (!ps2_wait_clk(LOW)) return false;
  if (digitalRead(PS2_DATA_PIN) != LOW) return false;  // no ACK
  if (!ps2_wait_clk(HIGH)) return false;

  // 7. Release bus
  ps2_release(PS2_CLK_PIN);
  ps2_release(PS2_DATA_PIN);
  return true;
}

// ============================================================
//  Pretty-print a read result
// ============================================================
void print_read(const char* label, int val) {
  Serial.print("  ");
  Serial.print(label);
  Serial.print(": ");
  if (val >= 0) {
    Serial.print("0x");
    if (val < 0x10) Serial.print("0");
    Serial.print(val, HEX);
    Serial.print(" (");
    Serial.print(val, DEC);
    Serial.println(")");
  } else {
    Serial.print("ERROR ");
    Serial.println(val);
  }
}

// ============================================================
//  Synaptics-specific: read the TouchPad ID bytes from the
//  extended identify sequence.
//  Standard PS/2 touchpads respond to:
//    Set Sample Rate 200, 100, 80 → then GetDeviceID returns
//    the real vendor/model IDs (Synaptics magic sequence).
// ============================================================
void synaptics_identify() {
  Serial.println("\n[Synaptics Extended Identify]");

  // Magic knock: SetSampleRate(200), SetSampleRate(100), SetSampleRate(80)
  uint8_t magic[] = {200, 100, 80};
  for (int i = 0; i < 3; i++) {
    if (!ps2_send_byte(0xF3)) { Serial.println("  Send SetSampleRate failed"); return; }
    delay(20);
    int ack = ps2_read_byte();
    print_read("  SetSampleRate ACK", ack);
    if (ack != 0xFA) { Serial.println("  Bad ACK, aborting identify"); return; }

    if (!ps2_send_byte(magic[i])) { Serial.println("  Send rate value failed"); return; }
    delay(20);
    ack = ps2_read_byte();
    print_read("  Rate value ACK", ack);
    if (ack != 0xFA) { Serial.println("  Bad ACK, aborting identify"); return; }
  }

  // Now GetDeviceID — Synaptics returns 3 ID bytes
  if (!ps2_send_byte(0xF2)) { Serial.println("  Send GetDeviceID failed"); return; }
  delay(30);
  int ack  = ps2_read_byte();
  int id1  = ps2_read_byte();
  int id2  = ps2_read_byte();
  print_read("GetDeviceID ACK", ack);
  print_read("Synaptics ID byte 1", id1);
  print_read("Synaptics ID byte 2", id2);

  if (id1 == 0x47 || id1 == 0x46) {
    Serial.println("  >> Synaptics touchpad CONFIRMED (ID byte matches)");
  } else {
    Serial.println("  >> Device ID doesn't match known Synaptics value");
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n============================================");
  Serial.println("  Synaptics T1321B — PS/2 Diagnostic Tool");
  Serial.println("  CLK: GPIO16 (RX2)   DATA: GPIO17 (TX2)");
  Serial.println("============================================\n");

  // Start with both lines released (open-collector idle HIGH)
  ps2_release(PS2_CLK_PIN);
  ps2_release(PS2_DATA_PIN);
  delay(200);

  // ---- Step 1: Wait for bus idle (device runs BAT after power-on, up to ~1.5s) ----
  Serial.println("[Bus Idle Check — waiting up to 1500ms for device startup]");
  unsigned long idleStart = millis();
  bool clk_hi = false, data_hi = false;
  while (millis() - idleStart < 1500) {
    clk_hi  = (digitalRead(PS2_CLK_PIN)  == HIGH);
    data_hi = (digitalRead(PS2_DATA_PIN) == HIGH);
    if (clk_hi && data_hi) break;
    delay(10);
  }
  Serial.print("  CLK  idle: ");
  Serial.println(clk_hi  ? "HIGH ✓" : "LOW  ✗  (check pull-up / wiring)");
  Serial.print("  DATA idle: ");
  Serial.println(data_hi ? "HIGH ✓" : "LOW  ✗  (check pull-up / wiring)");

  if (!clk_hi || !data_hi) {
    Serial.println("\n⚠ Bus not idle after 1500ms.");
    Serial.println("  Possible causes:");
    Serial.println("  → CLK or DATA wire shorted to GND");
    Serial.println("  → VCC pin not connected / wrong pin");
    Serial.println("  → Touchpad damaged");
    Serial.println("  Attempting RESET anyway...");
  }

  // ---- Step 2: Send RESET (0xFF) ----
  Serial.println("\n[Sending RESET (0xFF)]");
  bool sent = ps2_send_byte(0xFF);
  Serial.print("  Send result: ");
  Serial.println(sent ? "OK" : "FAILED (no ACK on CLK edges — check wiring)");

  if (!sent) {
    Serial.println("\n⚠ RESET send failed.");
    Serial.println("  → Confirm CLK→GPIO17, DATA→GPIO16.");
    Serial.println("  → Touchpad may not be responding.");
    return;
  }

  // ---- Step 3: Read reset responses ----
  // Device should reply: 0xFA (ACK), 0xAA (BAT OK), 0x00 (device ID)
  Serial.println("\n[Reset Response (expected: 0xFA, 0xAA, 0x00)]");
  delay(500);  // give device time to complete self-test

  int r1 = ps2_read_byte();
  int r2 = ps2_read_byte();
  int r3 = ps2_read_byte();

  print_read("Byte 1 (want 0xFA = ACK)",  r1);
  print_read("Byte 2 (want 0xAA = BAT OK)", r2);
  print_read("Byte 3 (want 0x00 = Device ID)", r3);

  // Evaluate
  bool ok = (r1 == 0xFA) && (r2 == 0xAA) && (r3 == 0x00);
  Serial.println();
  if (ok) {
    Serial.println("✅ Touchpad responded correctly to RESET!");
  } else {
    Serial.println("⚠ Unexpected response. Possible causes:");
    Serial.println("  - CLK/DATA swapped (try swapping GPIO16 and 17)");
    Serial.println("  - Missing or wrong pull-up resistors");
    Serial.println("  - Incorrect VCC (check datasheet — usually 5V or 3.3V)");
    Serial.println("  - Wrong connector pinout");
  }

  // ---- Step 4: Standard GetDeviceID (0xF2) ----
  Serial.println("\n[Standard GetDeviceID (0xF2)]");
  delay(100);
  bool sent2 = ps2_send_byte(0xF2);
  Serial.print("  Send result: ");
  Serial.println(sent2 ? "OK" : "FAILED");
  if (sent2) {
    delay(50);
    int ack  = ps2_read_byte();
    int devId = ps2_read_byte();
    print_read("ACK (want 0xFA)", ack);
    print_read("Device ID", devId);
    if (devId == 0x00) Serial.println("  >> Standard PS/2 mouse / touchpad");
  }

  // ---- Step 5: Synaptics extended identify ----
  synaptics_identify();

  // ---- Step 6: Enable data reporting ----
  Serial.println("\n[Enable Data Reporting (0xF4)]");
  delay(100);
  bool sent3 = ps2_send_byte(0xF4);
  Serial.print("  Send result: ");
  Serial.println(sent3 ? "OK" : "FAILED");
  if (sent3) {
    delay(50);
    int ack3 = ps2_read_byte();
    print_read("ACK (want 0xFA)", ack3);
    if (ack3 == 0xFA)
      Serial.println("  >> Data reporting enabled. Touch the pad to generate packets.");
    else
      Serial.println("  >> Unexpected ACK. Reporting may not be active.");
  }

  Serial.println("\n[Listening for data packets — touch the touchpad...]");
}

// ============================================================
//  LOOP — read and display raw PS/2 data packets
// ============================================================
void loop() {
  // Standard PS/2 mouse sends 3-byte packets
  // Synaptics in extended mode can send 6-byte packets
  // Here we read standard 3-byte packets for diagnostics

  int b0 = ps2_read_byte();
  if (b0 < 0) return;   // timeout — no data, keep looping

  int b1 = ps2_read_byte();
  int b2 = ps2_read_byte();

  if (b1 < 0 || b2 < 0) {
    Serial.print("Partial packet — b0=0x");
    Serial.println(b0, HEX);
    return;
  }

  // Decode standard PS/2 3-byte packet
  int8_t dx   = (int8_t)b1;
  int8_t dy   = (int8_t)b2;
  bool lb      = b0 & 0x01;   // left button
  bool rb      = b0 & 0x02;   // right button
  bool mb      = b0 & 0x04;   // middle button
  bool xsign   = b0 & 0x10;
  bool ysign   = b0 & 0x20;

  if (xsign) dx |= 0x100;  // sign extension already handled by int8_t cast
  if (ysign) dy = -dy;      // Y is inverted in PS/2

  Serial.printf("PKT  0x%02X 0x%02X 0x%02X  | dX=%+4d  dY=%+4d  | L=%d R=%d M=%d\n",
                (uint8_t)b0, (uint8_t)b1, (uint8_t)b2,
                dx, dy, lb, rb, mb);
}
