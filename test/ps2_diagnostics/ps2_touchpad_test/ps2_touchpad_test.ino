/*
  PS/2 Touchpad Connection Test — ESP32
  --------------------------------------
  Sends a Reset command to a PS/2 touchpad, checks for the correct
  self-test response, enables data reporting, then prints raw
  movement/button packets to the Serial Monitor as you touch the pad.

  WIRING:
    - Set CLK_PIN and DATA_PIN below to match your actual wiring.
    - If nothing responds, try swapping CLK_PIN and DATA_PIN first —
      this is the most common mistake.
    - Recommended: use a ~470-1k ohm series resistor on VCC for your
      first power-up, in case a pin is mis-wired.

  USAGE:
    - Open Serial Monitor at 115200 baud.
    - Watch the self-test result first.
    - Then move your finger on the pad and watch dX/dY/button values.
*/

#define CLK_PIN  16   // change to your wired clock pin
#define DATA_PIN 17   // change to your wired data pin

void clockHigh() { pinMode(CLK_PIN, INPUT_PULLUP); }
void clockLow()  { pinMode(CLK_PIN, OUTPUT); digitalWrite(CLK_PIN, LOW); }
void dataHigh()  { pinMode(DATA_PIN, INPUT_PULLUP); }
void dataLow()   { pinMode(DATA_PIN, OUTPUT); digitalWrite(DATA_PIN, LOW); }

bool waitForClock(int state, unsigned long timeoutUs = 25000) {
  unsigned long start = micros();
  while (digitalRead(CLK_PIN) != state) {
    if (micros() - start > timeoutUs) return false;
  }
  return true;
}

// Reads one byte sent by the touchpad (touchpad drives the clock).
// Consumes parity/stop bit clock edges but does not check parity —
// this is a connectivity test, not a production driver.
int ps2ReadByte(unsigned long startBitTimeoutUs = 25000) {
  if (!waitForClock(LOW, startBitTimeoutUs)) return -1;  // start bit

  unsigned long value = 0;
  for (int i = 0; i < 8; i++) {
    if (!waitForClock(HIGH)) return -1;
    if (!waitForClock(LOW)) return -1;
    value |= ((unsigned long)digitalRead(DATA_PIN) << i);
  }

  if (!waitForClock(HIGH)) return -1;         // parity bit edge
  if (!waitForClock(LOW)) return -1;
  if (!waitForClock(HIGH)) return -1;         // stop bit edge
  if (!waitForClock(LOW)) return -1;
  waitForClock(HIGH);                          // back to idle

  return (int)(value & 0xFF);
}

// Sends one byte to the touchpad (host-initiated write).
// Returns true if the touchpad acknowledged it.
bool ps2WriteByte(uint8_t data) {
  bool parity = true;

  clockLow();
  delayMicroseconds(150);
  dataLow();
  clockHigh();

  if (!waitForClock(LOW, 25000)) return false; // touchpad takes over clock

  for (int i = 0; i < 8; i++) {
    if (data & 0x01) dataHigh(); else dataLow();
    if (!waitForClock(HIGH)) return false;
    if (!waitForClock(LOW)) return false;
    parity ^= (data & 0x01);
    data >>= 1;
  }

  if (parity) dataHigh(); else dataLow();
  if (!waitForClock(HIGH)) return false;
  if (!waitForClock(LOW)) return false;

  dataHigh();                                   // stop bit
  if (!waitForClock(HIGH)) return false;
  if (!waitForClock(LOW)) return false;          // touchpad pulls clock to ack

  int ack = digitalRead(DATA_PIN);               // should read LOW
  waitForClock(HIGH);
  clockLow();                                    // idle: inhibit further sends

  return (ack == LOW);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== PS/2 Touchpad Connection Test ===");

  clockHigh();
  dataHigh();
  delay(100);

  Serial.println("Sending Reset (0xFF)...");
  if (!ps2WriteByte(0xFF)) {
    Serial.println("No ACK received. Check wiring, or try swapping CLK/DATA pins.");
    return;
  }

  clockHigh();
  int r1 = ps2ReadByte(600000);   // after reset, self-test can take up to ~500ms
  int r2 = ps2ReadByte(50000);
  Serial.print("Self-test response: 0x");
  Serial.print(r1, HEX);
  Serial.print(" 0x");
  Serial.println(r2, HEX);

  if (r1 == 0xAA && r2 == 0x00) {
    Serial.println("SUCCESS: touchpad is alive and PS/2-compatible.");
  } else {
    Serial.println("Unexpected response. Wiring may be swapped, or this isn't PS/2.");
  }

  Serial.println("Enabling data reporting (0xF4)...");
  clockHigh();
  dataHigh();
  if (!ps2WriteByte(0xF4)) {
    Serial.println("Enable-reporting command was not ACKed.");
  }

  Serial.println("Move your finger on the touchpad now:");
  Serial.println("--------------------------------------------------------");
}

void loop() {
  int b1 = ps2ReadByte();
  if (b1 < 0) return;
  int b2 = ps2ReadByte();
  int b3 = ps2ReadByte();
  if (b2 < 0 || b3 < 0) return;

  bool leftBtn   = b1 & 0x01;
  bool rightBtn  = b1 & 0x02;
  bool middleBtn = b1 & 0x04;
  int xSign = (b1 & 0x10) ? -1 : 1;
  int ySign = (b1 & 0x20) ? -1 : 1;

  int dx = xSign * b2;
  int dy = ySign * b3;

  Serial.print("dX: "); Serial.print(dx);
  Serial.print("  dY: "); Serial.print(dy);
  Serial.print("  L: "); Serial.print(leftBtn);
  Serial.print("  R: "); Serial.print(rightBtn);
  Serial.print("  M: "); Serial.println(middleBtn);
}