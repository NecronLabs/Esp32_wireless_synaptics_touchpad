// ============================================================
//  I2C Scanner for Arduino Uno — Synaptics TM3096 (T1321B)
//
//  Arduino Uno has FIXED I2C pins:
//    SDA = A4 (Pin 18)
//    SCL = A5 (Pin 19)
//
//  WIRING:
//    Touchpad SDA  → Arduino A4  + 4.7kΩ pull-up to 5V (or 3.3V)
//    Touchpad SCL  → Arduino A5  + 4.7kΩ pull-up to 5V (or 3.3V)
//    Touchpad VCC  → Arduino 5V  (try 3.3V if 5V doesn't work)
//    Touchpad GND  → Arduino GND
//
//  If you're unsure which touchpad wire is SDA vs SCL,
//  try both orientations — the scanner will tell you.
//
//  NOTE: You can reuse the same 10kΩ pull-ups you already have.
//
//  Open Serial Monitor at 115200 baud.
// ============================================================

#include <Wire.h>

// Known Synaptics I2C addresses
void printKnownDevice(uint8_t addr) {
  switch (addr) {
    case 0x20: Serial.print(F(" << Synaptics PS/2-over-SMBus")); break;
    case 0x2C: Serial.print(F(" << Synaptics RMI4 (common)")); break;
    case 0x46: Serial.print(F(" << Synaptics alternative")); break;
    case 0x70: Serial.print(F(" << Synaptics alternative")); break;
    case 0x4B: Serial.print(F(" << Synaptics/ALPS variant")); break;
    case 0x47: Serial.print(F(" << Possible Synaptics")); break;
    default: break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println(F("============================================="));
  Serial.println(F("  I2C / SMBus Scanner — Synaptics TM3096"));
  Serial.println(F("  Board : Arduino Uno"));
  Serial.println(F("  SDA   : A4 (fixed)"));
  Serial.println(F("  SCL   : A5 (fixed)"));
  Serial.println(F("============================================="));
  Serial.println();

  // Check if SDA and SCL lines are pulled high (idle)
  // On Uno, A4=SDA, A5=SCL
  pinMode(A4, INPUT);
  pinMode(A5, INPUT);
  delay(10);

  bool sdaHi = (digitalRead(A4) == HIGH);
  bool sclHi = (digitalRead(A5) == HIGH);
  Serial.print(F("  SDA (A4) idle: "));
  Serial.println(sdaHi ? F("HIGH  OK") : F("LOW   FAIL"));
  Serial.print(F("  SCL (A5) idle: "));
  Serial.println(sclHi ? F("HIGH  OK") : F("LOW   FAIL"));

  if (!sdaHi || !sclHi) {
    Serial.println(F("\n  WARNING: Bus lines not idle!"));
    Serial.println(F("  Check pull-up resistors and wiring."));
    Serial.println(F("  Scanning anyway...\n"));
  }

  // Initialize I2C
  Wire.begin();
  Wire.setClock(50000);   // 50 kHz — slow for reliability on test wiring
  delay(100);

  Serial.println(F("\n  Scanning all 127 I2C addresses...\n"));
  Serial.println(F("  Addr(hex)  Addr(dec)  Status"));
  Serial.println(F("  --------   --------   ------"));

  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();

    if (err == 0) {
      Serial.print(F("  0x"));
      if (addr < 0x10) Serial.print('0');
      Serial.print(addr, HEX);
      Serial.print(F("     "));
      if (addr < 100) Serial.print(' ');
      if (addr < 10)  Serial.print(' ');
      Serial.print(addr, DEC);
      Serial.print(F("      FOUND!"));
      printKnownDevice(addr);
      Serial.println();
      found++;
    } else if (err == 4) {
      Serial.print(F("  0x"));
      if (addr < 0x10) Serial.print('0');
      Serial.print(addr, HEX);
      Serial.println(F("              BUS ERROR"));
    }

    delay(3);
  }

  Serial.println();
  Serial.println(F("============================================="));
  if (found > 0) {
    Serial.print(F("  FOUND "));
    Serial.print(found);
    Serial.println(F(" device(s)!"));
    Serial.println(F("  >> Touchpad is I2C/SMBus based, NOT PS/2."));
    Serial.println(F("  >> Note the address for RMI4 driver use."));
  } else {
    Serial.println(F("  No I2C devices found."));
    Serial.println();
    Serial.println(F("  Try these fixes:"));
    Serial.println(F("  1. SWAP the two signal wires (SDA<->SCL)"));
    Serial.println(F("  2. Try VCC on 3.3V instead of 5V"));
    Serial.println(F("  3. Make sure pull-ups go to VCC (not separate)"));
    Serial.println(F("  4. Double-check GND is connected"));
    Serial.println(F("  5. Verify the ribbon cable is seated properly"));
  }
  Serial.println(F("============================================="));

  // If found, try reading a few bytes from each device
  if (found > 0) {
    Serial.println(F("\n  Attempting to read from found device(s)...\n"));
    for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() != 0) continue;

      // Try reading 4 bytes
      uint8_t bytesRead = Wire.requestFrom(addr, (uint8_t)4);
      if (bytesRead > 0) {
        Serial.print(F("  Device 0x"));
        if (addr < 0x10) Serial.print('0');
        Serial.print(addr, HEX);
        Serial.print(F(" returned "));
        Serial.print(bytesRead);
        Serial.print(F(" bytes: "));
        while (Wire.available()) {
          uint8_t b = Wire.read();
          Serial.print(F("0x"));
          if (b < 0x10) Serial.print('0');
          Serial.print(b, HEX);
          Serial.print(' ');
        }
        Serial.println();
      }
    }
  }

  Serial.println(F("\n  Scan complete. Scanner will re-scan every 10s.\n"));
}

// Re-scan periodically in case wires were changed
unsigned long lastScan = 0;

void loop() {
  if (millis() - lastScan < 10000) return;
  lastScan = millis();

  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (found == 0) {
        Serial.println(F("  [Re-scan] Devices found:"));
      }
      Serial.print(F("    0x"));
      if (addr < 0x10) Serial.print('0');
      Serial.print(addr, HEX);
      printKnownDevice(addr);
      Serial.println();
      found++;
    }
    delay(2);
  }
  if (found == 0) {
    Serial.println(F("  [Re-scan] No devices found"));
  }
}
