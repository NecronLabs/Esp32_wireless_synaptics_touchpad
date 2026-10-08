// ============================================================
//  ESP32 I2C Touchpad Test — Synaptics TM3096 @ 0x2C
//
//  Reads the HID-over-I2C descriptor, then continuously reads
//  input reports and prints raw data to serial monitor.
//
//  WIRING:
//    Touchpad SDA  → GPIO 21
//    Touchpad SCL  → GPIO 22
//    Touchpad VCC  → 3.3V
//    Touchpad GND  → GND
//
//  Serial Monitor: 115200 baud
// ============================================================

#include <Wire.h>

#define TP_ADDR       0x2C
#define I2C_SDA       21
#define I2C_SCL       22
#define I2C_SPEED     400000

// Common HID descriptor register addresses to try
uint16_t hidDescRegs[] = {0x0020, 0x0001, 0x0000};
int numHidDescRegs = 3;

// HID-over-I2C descriptor (30 bytes)
struct HidDescriptor {
  uint16_t wHIDDescLength;
  uint16_t bcdVersion;
  uint16_t wReportDescLength;
  uint16_t wReportDescRegister;
  uint16_t wInputRegister;
  uint16_t wMaxInputLength;
  uint16_t wOutputRegister;
  uint16_t wMaxOutputLength;
  uint16_t wCommandRegister;
  uint16_t wDataRegister;
  uint16_t wVendorID;
  uint16_t wProductID;
  uint16_t wVersionID;
  uint32_t reserved;
};

HidDescriptor hid;
bool hidFound = false;
uint16_t inputReg = 0;
uint16_t maxInputLen = 0;

// Read N bytes from a 16-bit register address
int readRegister(uint16_t reg, uint8_t* buf, uint16_t len) {
  Wire.beginTransmission(TP_ADDR);
  Wire.write((uint8_t)(reg & 0xFF));         // Register low byte
  Wire.write((uint8_t)((reg >> 8) & 0xFF));  // Register high byte
  uint8_t err = Wire.endTransmission(false);  // Repeated start
  if (err != 0) return -1;

  uint16_t got = Wire.requestFrom(TP_ADDR, len);
  if (got == 0) return -2;

  for (uint16_t i = 0; i < got && i < len; i++) {
    buf[i] = Wire.read();
  }
  return (int)got;
}

// Read from the input register (no register address — just read)
int readInput(uint8_t* buf, uint16_t maxLen) {
  uint16_t toRead = (maxLen < maxInputLen) ? maxLen : maxInputLen;
  if (toRead > 64) toRead = 64;  // Wire library limit

  // For input reports, send the input register address then read
  Wire.beginTransmission(TP_ADDR);
  Wire.write((uint8_t)(inputReg & 0xFF));
  Wire.write((uint8_t)((inputReg >> 8) & 0xFF));
  uint8_t err = Wire.endTransmission(false);
  if (err != 0) return -1;

  uint16_t got = Wire.requestFrom(TP_ADDR, toRead);
  if (got == 0) return -2;

  for (uint16_t i = 0; i < got; i++) {
    buf[i] = Wire.read();
  }
  return (int)got;
}

void printHex(uint8_t* buf, int len) {
  for (int i = 0; i < len; i++) {
    Serial.printf("%02X ", buf[i]);
    if ((i + 1) % 16 == 0) Serial.println();
  }
  if (len % 16 != 0) Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("=============================================");
  Serial.println("  Synaptics I2C Touchpad Test");
  Serial.println("  Address: 0x2C");
  Serial.println("=============================================");
  Serial.println();

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_SPEED);
  delay(100);

  // --- Step 1: Find HID descriptor ---
  Serial.println("[1] Searching for HID descriptor...");
  uint8_t descBuf[30];

  for (int i = 0; i < numHidDescRegs; i++) {
    uint16_t reg = hidDescRegs[i];
    Serial.printf("  Trying register 0x%04X... ", reg);

    int got = readRegister(reg, descBuf, 30);
    if (got < 30) {
      Serial.printf("FAIL (got %d bytes)\n", got);
      continue;
    }

    // Parse the descriptor
    memcpy(&hid, descBuf, 30);

    // Validate: length should be 30 (0x001E), version should be 0x0100
    if (hid.wHIDDescLength == 30 && (hid.bcdVersion == 0x0100 || hid.bcdVersion == 0x0200)) {
      hidFound = true;
      inputReg = hid.wInputRegister;
      maxInputLen = hid.wMaxInputLength;
      Serial.println("FOUND!");
      break;
    } else {
      Serial.printf("BAD (len=%d ver=0x%04X)\n", hid.wHIDDescLength, hid.bcdVersion);

      // Print raw bytes anyway for debugging
      Serial.print("  Raw: ");
      printHex(descBuf, 30);
    }
  }

  if (!hidFound) {
    Serial.println("\n  HID descriptor not found at common registers.");
    Serial.println("  Trying raw read from device...\n");

    // Fallback: just try to read whatever the device sends
    uint8_t raw[64];
    uint16_t got = Wire.requestFrom(TP_ADDR, (uint16_t)64);
    if (got > 0) {
      for (uint16_t i = 0; i < got; i++) raw[i] = Wire.read();
      Serial.printf("  Raw read (%d bytes):\n  ", got);
      printHex(raw, got);
    } else {
      Serial.println("  No data from raw read either.");
    }

    // Set defaults for brute-force reading
    inputReg = 0x0003;  // Common default input register
    maxInputLen = 32;
    Serial.printf("\n  Will try polling input register 0x%04X with %d byte reads.\n", inputReg, maxInputLen);
    Serial.println();
  }

  if (hidFound) {
    Serial.println("\n  --- HID Descriptor ---");
    Serial.printf("  Length:           %d\n", hid.wHIDDescLength);
    Serial.printf("  Version:          0x%04X\n", hid.bcdVersion);
    Serial.printf("  Report Desc Len:  %d bytes\n", hid.wReportDescLength);
    Serial.printf("  Report Desc Reg:  0x%04X\n", hid.wReportDescRegister);
    Serial.printf("  Input Register:   0x%04X\n", hid.wInputRegister);
    Serial.printf("  Max Input Length: %d bytes\n", hid.wMaxInputLength);
    Serial.printf("  Output Register:  0x%04X\n", hid.wOutputRegister);
    Serial.printf("  Max Output Len:   %d bytes\n", hid.wMaxOutputLength);
    Serial.printf("  Command Register: 0x%04X\n", hid.wCommandRegister);
    Serial.printf("  Data Register:    0x%04X\n", hid.wDataRegister);
    Serial.printf("  Vendor ID:        0x%04X\n", hid.wVendorID);
    Serial.printf("  Product ID:       0x%04X\n", hid.wProductID);
    Serial.printf("  Version ID:       0x%04X\n", hid.wVersionID);

    // --- Step 2: Read Report Descriptor ---
    if (hid.wReportDescLength > 0 && hid.wReportDescLength < 1000) {
      Serial.printf("\n[2] Reading Report Descriptor (%d bytes)...\n", hid.wReportDescLength);

      uint8_t* rdBuf = (uint8_t*)malloc(hid.wReportDescLength);
      if (rdBuf) {
        // Read in chunks (Wire library has 128-byte buffer on ESP32)
        int totalRead = 0;
        int remaining = hid.wReportDescLength;
        bool firstChunk = true;

        // Send register address
        Wire.beginTransmission(TP_ADDR);
        Wire.write((uint8_t)(hid.wReportDescRegister & 0xFF));
        Wire.write((uint8_t)((hid.wReportDescRegister >> 8) & 0xFF));
        Wire.endTransmission(false);

        while (remaining > 0) {
          int chunk = (remaining > 64) ? 64 : remaining;
          int got = Wire.requestFrom(TP_ADDR, (uint16_t)chunk);
          for (int i = 0; i < got; i++) {
            rdBuf[totalRead++] = Wire.read();
          }
          remaining -= got;
          if (got == 0) break;
        }

        Serial.printf("  Got %d bytes:\n  ", totalRead);
        printHex(rdBuf, totalRead);
        free(rdBuf);
      }
    }

    // --- Step 3: Send RESET command ---
    Serial.println("\n[3] Sending HID RESET command...");
    Wire.beginTransmission(TP_ADDR);
    Wire.write((uint8_t)(hid.wCommandRegister & 0xFF));
    Wire.write((uint8_t)((hid.wCommandRegister >> 8) & 0xFF));
    Wire.write(0x00);  // Reset command (opcode 0x0100 low byte)
    Wire.write(0x01);  // Reset command (opcode 0x0100 high byte)
    uint8_t err = Wire.endTransmission();
    Serial.printf("  Result: %s\n", err == 0 ? "OK" : "FAIL");
    delay(500);

    // Drain any reset response
    uint8_t tmp[64];
    Wire.requestFrom(TP_ADDR, (uint16_t)2);
    while (Wire.available()) Wire.read();
  }

  Serial.println("\n=============================================");
  Serial.println("  Reading input reports...");
  Serial.println("  Touch the pad! Data will print below.");
  Serial.println("=============================================\n");
}

unsigned long lastDataMs = 0;
uint32_t pktCount = 0;
uint32_t emptyCount = 0;

void loop() {
  uint8_t buf[64];
  memset(buf, 0, 64);

  int got = readInput(buf, maxInputLen);

  if (got > 2) {
    // HID-over-I2C input report: first 2 bytes = length (little-endian)
    uint16_t reportLen = buf[0] | (buf[1] << 8);

    if (reportLen > 2 && reportLen <= got) {
      pktCount++;

      // Print report (skip the 2-byte length header)
      Serial.printf("[%06lu] Len=%2d | ", pktCount, reportLen - 2);
      for (int i = 2; i < reportLen && i < got; i++) {
        Serial.printf("%02X ", buf[i]);
      }

      // Try to interpret as touchpad data
      // Common HID touchpad report: ReportID, ContactCount, ContactID, X_L, X_H, Y_L, Y_H, Tip, ...
      if (reportLen > 4) {
        uint8_t reportId = buf[2];
        Serial.printf(" | ID=%d", reportId);
      }

      Serial.println();
      lastDataMs = millis();
    } else if (reportLen == 0) {
      // Empty report — no data available
      emptyCount++;
    }
  }

  // Periodic status
  if (millis() - lastDataMs > 3000 && lastDataMs > 0) {
    Serial.printf("  ... waiting (pkts=%lu, empty=%lu)\n", pktCount, emptyCount);
    lastDataMs = millis();
  } else if (lastDataMs == 0 && millis() > 5000 && (millis() % 3000) < 10) {
    Serial.printf("  ... no data yet (empty=%lu). Touch the pad.\n", emptyCount);
  }

  delay(5);  // ~200 Hz polling rate
}
