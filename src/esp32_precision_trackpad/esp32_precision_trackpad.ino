// ============================================================
//  ESP32 BLE Precision Touchpad (PTP)
//  Synaptics TM3096 (T1321B) → PS/2 Absolute Mode → Native PTP
//
//  WIRING:
//    Touchpad VCC  → ESP32 3.3V
//    Touchpad GND  → ESP32 GND
//    Touchpad CLK  → GPIO 16  + 10kΩ pull-up to 3.3V
//    Touchpad DATA → GPIO 17  + 10kΩ pull-up to 3.3V
// ============================================================

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>

#define PS2_CLK_PIN    16
#define PS2_DATA_PIN   17
#define LED_PIN        2

// PTP HID Report Descriptor
const uint8_t hidReportDescriptor[] = {
  0x05, 0x0D,                         // Usage Page (Digitizer)
  0x09, 0x05,                         // Usage (Touch Pad)
  0xA1, 0x01,                         // Collection (Application)
  0x85, 0x01,                         //   Report ID (1)

  // -- FINGER 1 --
  0x05, 0x0D,                         //   Usage Page (Digitizer)
  0x09, 0x22,                         //   Usage (Finger)
  0xA1, 0x02,                         //   Collection (Logical)
  0x09, 0x42,                         //     Usage (Tip Switch)
  0x15, 0x00,                         //     Logical Minimum (0)
  0x25, 0x01,                         //     Logical Maximum (1)
  0x75, 0x01,                         //     Report Size (1)
  0x95, 0x01,                         //     Report Count (1)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x09, 0x47,                         //     Usage (Confidence)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x09, 0x51,                         //     Usage (Contact Identifier)
  0x25, 0x7F,                         //     Logical Maximum (127)
  0x75, 0x06,                         //     Report Size (6)
  0x95, 0x01,                         //     Report Count (1)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
  0x09, 0x30,                         //     Usage (X)
  0x26, 0xFF, 0x1F,                   //     Logical Maximum (8191)
  0x75, 0x10,                         //     Report Size (16)
  0x95, 0x01,                         //     Report Count (1)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x09, 0x31,                         //     Usage (Y)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0xC0,                               //   End Collection

  // -- FINGER 2 --
  0x05, 0x0D,                         //   Usage Page (Digitizer)
  0x09, 0x22,                         //   Usage (Finger)
  0xA1, 0x02,                         //   Collection (Logical)
  0x09, 0x42,                         //     Usage (Tip Switch)
  0x15, 0x00,                         //     Logical Minimum (0)
  0x25, 0x01,                         //     Logical Maximum (1)
  0x75, 0x01,                         //     Report Size (1)
  0x95, 0x01,                         //     Report Count (1)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x09, 0x47,                         //     Usage (Confidence)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x09, 0x51,                         //     Usage (Contact Identifier)
  0x25, 0x7F,                         //     Logical Maximum (127)
  0x75, 0x06,                         //     Report Size (6)
  0x95, 0x01,                         //     Report Count (1)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
  0x09, 0x30,                         //     Usage (X)
  0x26, 0xFF, 0x1F,                   //     Logical Maximum (8191)
  0x75, 0x10,                         //     Report Size (16)
  0x95, 0x01,                         //     Report Count (1)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x09, 0x31,                         //     Usage (Y)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0xC0,                               //   End Collection

  // -- FINGER 3 --
  0x05, 0x0D,                         //   Usage Page (Digitizer)
  0x09, 0x22,                         //   Usage (Finger)
  0xA1, 0x02,                         //   Collection (Logical)
  0x09, 0x42,                         //     Usage (Tip Switch)
  0x15, 0x00,                         //     Logical Minimum (0)
  0x25, 0x01,                         //     Logical Maximum (1)
  0x75, 0x01,                         //     Report Size (1)
  0x95, 0x01,                         //     Report Count (1)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x09, 0x47,                         //     Usage (Confidence)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x09, 0x51,                         //     Usage (Contact Identifier)
  0x25, 0x7F,                         //     Logical Maximum (127)
  0x75, 0x06,                         //     Report Size (6)
  0x95, 0x01,                         //     Report Count (1)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x05, 0x01,                         //     Usage Page (Generic Desktop Ctrls)
  0x09, 0x30,                         //     Usage (X)
  0x26, 0xFF, 0x1F,                   //     Logical Maximum (8191)
  0x75, 0x10,                         //     Report Size (16)
  0x95, 0x01,                         //     Report Count (1)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0x09, 0x31,                         //     Usage (Y)
  0x81, 0x02,                         //     Input (Data,Var,Abs)
  0xC0,                               //   End Collection

  // -- SCAN TIME --
  0x05, 0x0D,                         //   Usage Page (Digitizer)
  0x09, 0x56,                         //   Usage (Scan Time)
  0x27, 0xFF, 0xFF, 0x00, 0x00,       //   Logical Maximum (65535)
  0x75, 0x10,                         //   Report Size (16)
  0x95, 0x01,                         //   Report Count (1)
  0x81, 0x02,                         //   Input (Data,Var,Abs)

  // -- CONTACT COUNT --
  0x05, 0x0D,                         //   Usage Page (Digitizer)
  0x09, 0x54,                         //   Usage (Contact Count)
  0x25, 0x7F,                         //   Logical Maximum (127)
  0x75, 0x08,                         //   Report Size (8)
  0x95, 0x01,                         //   Report Count (1)
  0x81, 0x02,                         //   Input (Data,Var,Abs)

  // -- BUTTON --
  0x05, 0x09,                         //   Usage Page (Button)
  0x19, 0x01,                         //   Usage Minimum (Button 1)
  0x29, 0x02,                         //   Usage Maximum (Button 2)
  0x15, 0x00,                         //   Logical Minimum (0)
  0x25, 0x01,                         //   Logical Maximum (1)
  0x75, 0x01,                         //   Report Size (1)
  0x95, 0x02,                         //   Report Count (2)
  0x81, 0x02,                         //   Input (Data,Var,Abs)
  
  // -- PADDING --
  0x75, 0x06,                         //   Report Size (6)
  0x95, 0x01,                         //   Report Count (1)
  0x81, 0x03,                         //   Input (Const,Var,Abs)

  // ==========================================
  // MAXIMUM CONTACT COUNT FEATURE REPORT
  // ==========================================
  0x85, 0x02,                         //   Report ID (2)
  0x05, 0x0D,                         //   Usage Page (Digitizer)
  0x09, 0x55,                         //   Usage (Contact Count Maximum)
  0x25, 0x03,                         //   Logical Maximum (3) - Max 3 fingers
  0x75, 0x08,                         //   Report Size (8)
  0x95, 0x01,                         //   Report Count (1)
  0xB1, 0x02,                         //   Feature (Data,Var,Abs)

  // ==========================================
  // PTP CERTIFICATION STATUS FEATURE REPORT
  // ==========================================
  0x85, 0x03,                         //   Report ID (3)
  0x06, 0x00, 0xFF,                   //   Usage Page (Vendor Defined 0xFF00)
  0x09, 0xC5,                         //   Usage (0xC5)
  0x15, 0x00,                         //   Logical Minimum (0)
  0x26, 0xFF, 0x00,                   //   Logical Maximum (255)
  0x75, 0x08,                         //   Report Size (8)
  0x96, 0x00, 0x01,                   //   Report Count (256)
  0xB1, 0x02,                         //   Feature (Data,Var,Abs)
  
  0xC0                                // End Collection
};

// PTP Report Structure
struct __attribute__((packed)) PtpContact {
  uint8_t  tip_id;  // bit0: Tip, bit1: Confidence, bit2-7: Contact ID
  uint16_t x;
  uint16_t y;
};

struct __attribute__((packed)) PtpReport {
  PtpContact fingers[3];
  uint16_t scanTime;
  uint8_t  contactCount;
  uint8_t  button; // bit0: Button 1, bit1-7: Padding
};

// BLE state
BLEHIDDevice* hid;
BLECharacteristic* input;
BLECharacteristic* feature;
bool connected = false;

class MyCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { connected = true; }
  void onDisconnect(BLEServer* pServer) { connected = false; }
};

void setupBLE() {
  BLEDevice::init("Precision Touchpad");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyCallbacks());

  hid = new BLEHIDDevice(pServer);
  input = hid->inputReport(1);   // Report ID 1 (Touch data)
  feature = hid->featureReport(2); // Report ID 2 (Contact Count Max)
  
  // Set the max contacts to 3
  uint8_t maxContacts[] = {0x03};
  feature->setValue(maxContacts, 1);

  // Set the PTP Certification blob (Report ID 3, 256 bytes)
  BLECharacteristic* certFeature = hid->featureReport(3);
  uint8_t certBlob[256] = {0}; 
  certFeature->setValue(certBlob, 256);

  hid->manufacturer()->setValue("ByteBuilds");
  hid->pnp(0x01, 0x02E5, 0x0111, 0x0110);
  hid->hidInfo(0x00, 0x01);
  
  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_BOND);

  hid->reportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
  hid->startServices();

  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->setAppearance(HID_MOUSE);
  pAdvertising->addServiceUUID(hid->hidService()->getUUID());
  pAdvertising->start();
}

// ======================== PS/2 DRIVER ==========================

#define PS2_BUF_SIZE  64
static volatile uint8_t ps2Buf[PS2_BUF_SIZE];
static volatile uint8_t ps2BufHead = 0;
static volatile uint8_t ps2BufTail = 0;

static volatile uint8_t  ps2BitCount = 0;
static volatile uint8_t  ps2InByte   = 0;
static volatile uint8_t  ps2Parity   = 0;
static volatile bool     ps2Error    = false;

void IRAM_ATTR ps2ClkISR() {
  uint8_t bit = digitalRead(PS2_DATA_PIN);
  switch (ps2BitCount) {
    case 0:
      if (bit != 0) { ps2Error = true; ps2BitCount = 0; return; }
      ps2InByte = 0; ps2Parity = 0;
      break;
    case 1: case 2: case 3: case 4:
    case 5: case 6: case 7: case 8:
      ps2InByte |= (bit << (ps2BitCount - 1));
      ps2Parity ^= bit;
      break;
    case 9:
      ps2Parity ^= bit;
      if (ps2Parity != 1) { ps2Error = true; ps2BitCount = 0; return; }
      break;
    case 10:
      if (bit != 1) { ps2Error = true; ps2BitCount = 0; return; }
      { uint8_t nh = (ps2BufHead + 1) & (PS2_BUF_SIZE - 1);
        if (nh != ps2BufTail) { ps2Buf[ps2BufHead] = ps2InByte; ps2BufHead = nh; }
      }
      ps2BitCount = 0; return;
  }
  ps2BitCount++;
}

int ps2Read() {
  if (ps2BufHead == ps2BufTail) return -1;
  uint8_t v = ps2Buf[ps2BufTail];
  ps2BufTail = (ps2BufTail + 1) & (PS2_BUF_SIZE - 1);
  return v;
}

int ps2Available() { return (ps2BufHead - ps2BufTail) & (PS2_BUF_SIZE - 1); }
void ps2Flush() { ps2BufTail = ps2BufHead; }

static inline void ps2Release(uint8_t pin) { pinMode(pin, INPUT); }
static inline void ps2Low(uint8_t pin) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }

static bool ps2WaitClk(uint8_t level, unsigned long timeoutUs = 100000UL) {
  unsigned long t0 = micros();
  while (digitalRead(PS2_CLK_PIN) != level) {
    if ((micros() - t0) > timeoutUs) return false;
  }
  return true;
}

bool ps2SendByte(uint8_t data) {
  uint8_t parity = 1;
  ps2Low(PS2_CLK_PIN); delayMicroseconds(150);
  ps2Low(PS2_DATA_PIN); delayMicroseconds(10);
  ps2Release(PS2_CLK_PIN);
  for (int i = 0; i < 8; i++) {
    if (!ps2WaitClk(LOW)) return false;
    uint8_t b = (data >> i) & 1; parity ^= b;
    if (b) ps2Release(PS2_DATA_PIN); else ps2Low(PS2_DATA_PIN);
    delayMicroseconds(50);
    if (!ps2WaitClk(HIGH)) return false;
  }
  if (!ps2WaitClk(LOW)) return false;
  if (parity) ps2Release(PS2_DATA_PIN); else ps2Low(PS2_DATA_PIN);
  delayMicroseconds(50);
  if (!ps2WaitClk(HIGH)) return false;
  if (!ps2WaitClk(LOW)) return false;
  ps2Release(PS2_DATA_PIN); delayMicroseconds(50);
  if (!ps2WaitClk(HIGH)) return false;
  if (!ps2WaitClk(LOW)) return false;
  if (digitalRead(PS2_DATA_PIN) != LOW) return false;
  if (!ps2WaitClk(HIGH)) return false;
  ps2Release(PS2_CLK_PIN); ps2Release(PS2_DATA_PIN);
  return true;
}

int ps2ReadBytePoll(unsigned long bitTimeoutUs = 100000UL) {
  uint8_t data = 0, parity = 1;
  ps2Release(PS2_CLK_PIN); ps2Release(PS2_DATA_PIN);
  if (!ps2WaitClk(LOW, bitTimeoutUs)) return -1;
  if (digitalRead(PS2_DATA_PIN) != LOW) return -2;
  if (!ps2WaitClk(HIGH, bitTimeoutUs)) return -1;
  for (int i = 0; i < 8; i++) {
    if (!ps2WaitClk(LOW, bitTimeoutUs)) return -1;
    uint8_t b = digitalRead(PS2_DATA_PIN);
    data |= (b << i); parity ^= b;
    if (!ps2WaitClk(HIGH, bitTimeoutUs)) return -1;
  }
  if (!ps2WaitClk(LOW, bitTimeoutUs)) return -1;
  parity ^= digitalRead(PS2_DATA_PIN);
  if (!ps2WaitClk(HIGH, bitTimeoutUs)) return -1;
  if (!ps2WaitClk(LOW, bitTimeoutUs)) return -1;
  if (digitalRead(PS2_DATA_PIN) != HIGH) return -3;
  if (!ps2WaitClk(HIGH, bitTimeoutUs)) return -1;
  if (parity != 0) return -4;
  return (int)data;
}

int ps2ReadByteQuick() { return ps2ReadBytePoll(10000UL); }
void ps2DrainBus() { delay(5); while (ps2ReadByteQuick() >= 0) {} }
int ps2Command(uint8_t cmd) {
  if (!ps2SendByte(cmd)) {
    ps2Low(PS2_CLK_PIN); delay(50); ps2Release(PS2_CLK_PIN); delay(20);
    if (!ps2SendByte(cmd)) return -1;
  }
  delay(15);
  return ps2ReadByteQuick();
}

void ps2Init() { ps2Release(PS2_CLK_PIN); ps2Release(PS2_DATA_PIN); delay(100); }

void ps2StartISR() {
  ps2BitCount = 0; ps2InByte = 0; ps2Parity = 0; ps2Error = false; 
  ps2BufHead = 0; ps2BufTail = 0;
  attachInterrupt(digitalPinToInterrupt(PS2_CLK_PIN), ps2ClkISR, FALLING);
}
void ps2StopISR() { detachInterrupt(digitalPinToInterrupt(PS2_CLK_PIN)); }

bool ps2SlicedCommand(uint8_t value) {
  int failures = 0;
  for (int i = 6; i >= 0; i -= 2) {
    uint8_t nibble = (value >> i) & 0x03;
    if (!ps2SendByte(0xE8)) { failures++; continue; }
    delay(15); ps2ReadByteQuick();
    if (!ps2SendByte(nibble)) { failures++; continue; }
    delay(15); ps2ReadByteQuick();
  }
  return (failures < 4);
}

bool synapticsSetMode(uint8_t mode) {
  ps2Low(PS2_CLK_PIN); delay(200); ps2Release(PS2_CLK_PIN); delay(100);
  ps2DrainBus();
  ps2Command(0xE6);
  ps2Command(0xF5);
  delay(30); ps2DrainBus();
  ps2SlicedCommand(mode);
  ps2Command(0xF3); ps2Command(0x14);
  ps2Command(0xF4);
  return true;
}

// ======================== TOUCH DATA ===========================

#define FINGER_Z_OFF 12

struct TouchData {
  uint16_t x, y;
  uint8_t  z, w;
  uint16_t x2, y2;
  uint8_t  z2;
  bool     btnL, btnR, btnM;
  uint8_t  fingers;
  bool     valid;
  bool     isAgm;
};

static TouchData gTouch;

void parseAbsolutePacket(uint8_t *buf) {
  TouchData &t = gTouch;
  if ((buf[0] & 0xC8) != 0x80 || (buf[3] & 0xC8) != 0xC0) {
    t.valid = false;
    return;
  }

  t.x = (uint16_t)(((buf[3] & 0x10) << 8) | ((buf[1] & 0x0F) << 8) | buf[4]);
  t.y = (uint16_t)(((buf[3] & 0x20) << 7) | ((buf[1] & 0xF0) << 4) | buf[5]);
  t.z = buf[2];
  t.w = (uint8_t)(((buf[0] & 0x30) >> 2) | ((buf[0] & 0x04) >> 1) | ((buf[3] & 0x04) >> 2));
  
  t.btnL = buf[0] & 0x01;
  t.btnR = buf[0] & 0x02;
  t.btnM = ((buf[0] ^ buf[3]) & 0x01);

  static uint16_t lastAgmX = 0, lastAgmY = 0;
  static uint8_t  lastAgmZ = 0;
  t.isAgm = false;

  if (t.z < FINGER_Z_OFF) {
    t.fingers = 0;
    t.x2 = 0; t.y2 = 0; t.z2 = 0;
  } else if (t.w == 2) {
    t.isAgm = true;
    lastAgmX = t.x; lastAgmY = t.y; lastAgmZ = t.z;
    t.fingers = 2;
  } else {
    if (t.w >= 4) {
      t.fingers = 1;
      t.x2 = 0; t.y2 = 0; t.z2 = 0;
    } else if (t.w == 0 || t.w == 1) {
      t.fingers = (t.w == 0) ? 2 : 3;
      t.x2 = lastAgmX;
      t.y2 = lastAgmY;
      t.z2 = lastAgmZ;
    } else {
      t.fingers = 1;
    }
  }
  t.valid = true;
}

// ======================== PTP REPORT BUILDER ===================

// Keep track of active fingers for PTP lift reports
static bool f1_active = false;
static bool f2_active = false;
static bool f3_active = false;

void sendPtpReport() {
  if (!connected) return;

  PtpReport report = {0};
  
  // Basic button mapping
  if (gTouch.btnL) report.button |= 0x01;
  if (gTouch.btnR) report.button |= 0x02;
  
  // Trackpad X/Y values from Synaptics are naturally inverted compared to PTP sometimes, 
  // but let's assume they map directly 0-8191. If inverted, Windows settings can invert scrolling.
  // We need to invert Y so that 0 is at the top (Synaptics 0 is at the bottom)
  uint16_t mappedY1 = 8191 - gTouch.y;
  uint16_t mappedY2 = 8191 - gTouch.y2;

  // Contact Count
  report.contactCount = gTouch.fingers;
  if (report.contactCount > 3) report.contactCount = 3;

  // Finger 1
  if (gTouch.fingers >= 1) {
    report.fingers[0].tip_id = 0x01 | 0x02 | (1 << 2); // Tip=1, Conf=1, ID=1
    report.fingers[0].x = gTouch.x;
    report.fingers[0].y = mappedY1;
    f1_active = true;
  } else if (f1_active) {
    // Send lift report
    report.fingers[0].tip_id = 0x00 | 0x00 | (1 << 2); // Tip=0, Conf=0, ID=1
    f1_active = false;
  }

  // Finger 2
  if (gTouch.fingers >= 2) {
    report.fingers[1].tip_id = 0x01 | 0x02 | (2 << 2); // Tip=1, Conf=1, ID=2
    report.fingers[1].x = gTouch.x2;
    report.fingers[1].y = mappedY2;
    f2_active = true;
  } else if (f2_active) {
    report.fingers[1].tip_id = 0x00 | 0x00 | (2 << 2);
    f2_active = false;
  }

  // Finger 3 (Faked position if W=1)
  if (gTouch.fingers >= 3) {
    report.fingers[2].tip_id = 0x01 | 0x02 | (3 << 2); // Tip=1, Conf=1, ID=3
    // Fake the 3rd finger slightly offset from the 2nd finger
    report.fingers[2].x = gTouch.x2 + 200;
    report.fingers[2].y = mappedY2;
    f3_active = true;
  } else if (f3_active) {
    report.fingers[2].tip_id = 0x00 | 0x00 | (3 << 2);
    f3_active = false;
  }

  report.scanTime = (uint16_t)(micros() / 100);

  static uint8_t last_button = 0;
  static bool was_idle = true;
  bool is_idle = (gTouch.fingers == 0 && !f1_active && !f2_active && !f3_active);
  
  if (is_idle && was_idle && report.button == last_button) {
    return; // Completely idle, do nothing
  }
  
  // Rate limit to ~80Hz (12ms) to prevent BLE buffer overflow latency
  static unsigned long last_send = 0;
  // Send immediately if lifting fingers or pressing button
  bool urgent = (is_idle && !was_idle) || (report.button != last_button);
  
  if (!urgent && (millis() - last_send < 12)) {
    return;
  }
  
  was_idle = is_idle;
  last_button = report.button;
  last_send = millis();

  input->setValue((uint8_t*)&report, sizeof(PtpReport));
  input->notify();
}

// ======================== MAIN ==================================

static int pktIndex = 0;
static uint8_t pktBytes[6] = {0};

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n--- ESP32 Precision Touchpad ---");
  
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  
  ps2Init();
  delay(1500); // Wait for touchpad boot
  ps2DrainBus();
  
  // Set Synaptics Absolute Mode
  Serial.println("Activating Absolute Mode...");
  synapticsSetMode(0xC1);
  delay(100);
  ps2DrainBus();
  
  ps2StartISR();
  setupBLE();
  Serial.println("Bluetooth started! Pair 'Precision Touchpad'.");
}

void loop() {
  if (connected) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    // Blink if disconnected
    digitalWrite(LED_PIN, (millis() / 500) % 2);
  }

  while (ps2Available() > 0) {
    int b = ps2Read();
    if (b < 0) break;

    if (pktIndex == 0) {
      if ((b & 0xC8) != 0x80) continue;
      pktBytes[0] = (uint8_t)b;
      pktIndex = 1;
    } else if (pktIndex < 3) {
      pktBytes[pktIndex] = (uint8_t)b;
      pktIndex++;
    } else if (pktIndex == 3) {
      if ((b & 0xC8) != 0xC0) {
        if ((b & 0xC8) == 0x80) { pktBytes[0] = (uint8_t)b; pktIndex = 1; } 
        else pktIndex = 0;
        continue;
      }
      pktBytes[3] = (uint8_t)b;
      pktIndex = 4;
    } else if (pktIndex < 6) {
      pktBytes[pktIndex] = (uint8_t)b;
      pktIndex++;

      if (pktIndex == 6) {
        pktIndex = 0;
        parseAbsolutePacket(pktBytes);
        
        if (gTouch.valid) {
          // If it's an AGM packet (W=2), wait for the primary packet (W=0 or W=1)
          // before sending the report to Windows. This ensures both finger coords are synchronized.
          if (!gTouch.isAgm) {
            sendPtpReport();
          }
        }
      }
    }
  }
}
