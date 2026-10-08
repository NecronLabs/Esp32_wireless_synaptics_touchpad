// ============================================================
//  ESP32 BLE Wireless Trackpad — Phase 2
//  Synaptics TM3096 (T1321B) → Absolute Mode → BLE HID Mouse
//
//  FEATURES:
//    - Synaptics Absolute Mode (6-byte packets)
//    - 1-finger move → cursor
//    - 1-finger tap → left click
//    - 2-finger scroll (vertical + horizontal)
//    - 2-finger tap → right click
//    - Physical button support
//    - Falls back to standard 3-byte mode if absolute fails
//
//  WIRING:
//    Touchpad VCC  → ESP32 3.3V
//    Touchpad GND  → ESP32 GND
//    Touchpad CLK  → GPIO 16  + 10kΩ pull-up to 3.3V
//    Touchpad DATA → GPIO 17  + 10kΩ pull-up to 3.3V
//
//  LIBRARY: ESP32-BLE-Mouse by T-vK
//  Serial Monitor: 115200 baud
// ============================================================

#include <BleMouse.h>
#include <math.h>

// ======================== CONFIGURATION ========================

// --- Pin assignments ---
#define PS2_CLK_PIN    16
#define PS2_DATA_PIN   17
#define LED_PIN        2     // Built-in blue LED

// --- Cursor sensitivity ---
// Applied to absolute deltas before sending over BLE.
// Adjust if cursor feels too fast or too slow.
#define SENSITIVITY_ABS  0.3f   // For absolute mode (deltas are large)
#define SENSITIVITY_REL  1.0f   // For standard mode fallback

// --- Scroll settings ---
#define SCROLL_DIVISOR     230    // Higher = slower scroll (abs Y delta ÷ this)
#define SCROLL_H_DIVISOR   230    // Same for horizontal
#define SCROLL_INVERT_Y    true // true = natural scrolling (Mac-style)
#define SCROLL_INVERT_X    true

// --- Touch detection ---
#define FINGER_Z_ON        30    // Z pressure above this = finger touching
#define FINGER_Z_OFF       20    // Z pressure below this = finger lifted (hysteresis)
#define DEAD_ZONE           2    // Ignore abs deltas smaller than this (noise filter)

// --- Tap detection ---
#define TAP_MAX_DURATION_MS   200   // Max touch duration for a tap
#define TAP_MAX_MOVEMENT      80    // Max cumulative absolute movement for a tap
#define TAP_CLICK_HOLD_MS     60    // Virtual click hold duration (for 2F tap right-click)
#define TAP_2F_MAX_DURATION   350   // Longer window for 2-finger tap (fingers land at different times)
#define TAP_2F_MAX_MOVEMENT   150   // More movement allowed for 2-finger tap (2 fingers jitter more)

// --- Drag detection ---
#define DRAG_DETECT_MS        300   // ms to wait for finger return after tap (tap-and-drag)

// --- Momentum / kinetic scrolling ---
#define MOMENTUM_DECAY        0.93f // Velocity multiplied by this each tick (lower = stops faster)
#define MOMENTUM_INTERVAL_MS  15    // ms between momentum scroll events
#define MOMENTUM_MIN_VEL      0.3f  // Stop momentum below this velocity
#define MOMENTUM_MAX_VEL     ial momentum velocity
#define SCROLL_VELOCITY_SMOOTH 0.3f // Smoothing factor for velocity tracking (0-1, lower = smoother)

// --- PS/2 timing ---
#define PS2_SEND_TIMEOUT_US   100000UL
#define PS2_SEND_BIT_DELAY    50

// --- Synaptics absolute mode byte ---
// 0xC1 = Absolute(bit7) + HighRate(bit6) + WMode(bit0)
#define SYNAPTICS_MODE_BYTE   0xC1

// ======================== BLE MOUSE ============================

BleMouse bleMouse("ESP32 Trackpad", "ByteBuilds", 100);

// ======================== PS/2 DRIVER ==========================
// Interrupt-driven receive + bit-banged send (unchanged from Phase 1)
// ===============================================================

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

static bool ps2WaitClk(uint8_t level, unsigned long timeoutUs = PS2_SEND_TIMEOUT_US) {
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
    delayMicroseconds(PS2_SEND_BIT_DELAY);
    if (!ps2WaitClk(HIGH)) return false;
  }
  if (!ps2WaitClk(LOW)) return false;
  if (parity) ps2Release(PS2_DATA_PIN); else ps2Low(PS2_DATA_PIN);
  delayMicroseconds(PS2_SEND_BIT_DELAY);
  if (!ps2WaitClk(HIGH)) return false;
  if (!ps2WaitClk(LOW)) return false;
  ps2Release(PS2_DATA_PIN); delayMicroseconds(PS2_SEND_BIT_DELAY);
  if (!ps2WaitClk(HIGH)) return false;
  if (!ps2WaitClk(LOW)) return false;
  if (digitalRead(PS2_DATA_PIN) != LOW) return false;
  if (!ps2WaitClk(HIGH)) return false;
  ps2Release(PS2_CLK_PIN); ps2Release(PS2_DATA_PIN);
  return true;
}

int ps2ReadBytePoll(unsigned long bitTimeoutUs = PS2_SEND_TIMEOUT_US) {
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

// Quick read with 10ms timeout — used for draining ACKs during setup
int ps2ReadByteQuick() { return ps2ReadBytePoll(10000UL); }

// Drain all pending bytes from the bus (quick timeout)
void ps2DrainBus() {
  delay(5);
  while (ps2ReadByteQuick() >= 0) {}
}

// Send a command and read ACK. Retries once on send failure.
int ps2Command(uint8_t cmd) {
  if (!ps2SendByte(cmd)) {
    // Retry after brief bus inhibition
    ps2Low(PS2_CLK_PIN);
    delay(50);
    ps2Release(PS2_CLK_PIN);
    delay(20);
    if (!ps2SendByte(cmd)) return -1;
  }
  delay(15);
  return ps2ReadByteQuick();
}

void ps2Init() { ps2Release(PS2_CLK_PIN); ps2Release(PS2_DATA_PIN); delay(100); }

void ps2StartISR() {
  ps2BitCount = 0; ps2InByte = 0; ps2Parity = 0;
  ps2Error = false; ps2BufHead = 0; ps2BufTail = 0;
  attachInterrupt(digitalPinToInterrupt(PS2_CLK_PIN), ps2ClkISR, FALLING);
}

void ps2StopISR() { detachInterrupt(digitalPinToInterrupt(PS2_CLK_PIN)); }


// ================ SYNAPTICS SLICED COMMAND =====================
// Encodes an 8-bit value using 4x SetResolution (0xE8) commands,
// each carrying 2 bits. Used to set the touchpad mode byte.
//
// IMPORTANT: Does NOT abort on individual ACK failures. Some
// touchpads process commands correctly but return garbled ACKs.
// ===============================================================

bool ps2SlicedCommand(uint8_t value) {
  int failures = 0;
  for (int i = 6; i >= 0; i -= 2) {
    uint8_t nibble = (value >> i) & 0x03;

    // Send Set Resolution (0xE8)
    if (!ps2SendByte(0xE8)) { failures++; continue; }
    delay(15);
    int ack = ps2ReadByteQuick();
    Serial.printf("    E8 ack=0x%02X ", ack);

    // Send the 2-bit nibble
    if (!ps2SendByte(nibble)) { failures++; continue; }
    delay(15);
    ack = ps2ReadByteQuick();
    Serial.printf(" %d ack=0x%02X\n", nibble, ack);
  }
  Serial.printf("  Sliced command: %d/8 sends had issues\n", failures);
  return (failures < 4);  // Allow some failures — pad may still have processed it
}

// Set the Synaptics mode byte with retries and relaxed error handling
// Returns true if the mode was likely set (we verify in the loop via auto-detect)
bool synapticsSetMode(uint8_t mode) {
  Serial.printf("  Target mode byte: 0x%02X\n", mode);

  // Step 1: Inhibit bus — force touchpad to stop sending data
  Serial.println("  Inhibiting bus...");
  ps2Low(PS2_CLK_PIN);
  delay(200);
  ps2Release(PS2_CLK_PIN);
  delay(100);

  // Step 2: Drain any pending bytes
  ps2DrainBus();

  // Step 3: Send E6 (Set Scaling 1:1) as inert command to reset command state
  Serial.print("  Inert cmd (E6): ");
  int ack = ps2Command(0xE6);
  Serial.printf("ack=0x%02X\n", ack);

  // Step 4: Disable reporting
  Serial.print("  Disable reporting (F5): ");
  ack = ps2Command(0xF5);
  Serial.printf("ack=0x%02X\n", ack);
  // Don't abort — continue even if this fails

  delay(30);
  ps2DrainBus();

  // Step 5: Sliced command with mode byte
  Serial.println("  Sending sliced command...");
  ps2SlicedCommand(mode);

  // Step 6: Commit with SetSampleRate(20)
  Serial.print("  Commit (F3 14): ");
  ack = ps2Command(0xF3);
  Serial.printf("F3 ack=0x%02X, ", ack);
  ack = ps2Command(0x14);
  Serial.printf("14 ack=0x%02X\n", ack);

  // Step 7: Enable reporting
  Serial.print("  Enable reporting (F4): ");
  ack = ps2Command(0xF4);
  Serial.printf("ack=0x%02X\n", ack);

  Serial.println("  Command sequence complete (will auto-detect mode from packets)");
  return true;  // Optimistically return true — auto-detect will verify
}


// ======================== TOUCH DATA ===========================

struct TouchData {
  uint16_t x;          // 0-8191 absolute X (Primary finger)
  uint16_t y;          // 0-8191 absolute Y (Primary finger)
  uint8_t  z;          // 0-255 pressure (Primary finger)
  
  uint16_t x2;         // Absolute X (Secondary finger, via AGM)
  uint16_t y2;         // Absolute Y (Secondary finger, via AGM)
  uint8_t  z2;         // Pressure (Secondary finger, via AGM)
  
  uint8_t  w;          // 0-15 finger width / mode
  bool     btnL;       // Left button
  bool     btnR;       // Right button
  bool     btnM;       // Middle button
  uint8_t  fingers;    // Derived: 0=no touch, 1=one, 2=two, 3=three+
  bool     valid;      // Parsing succeeded
  bool     isAgm;      // True if this was an Advanced Gesture Mode (W=2) packet
};

static TouchData gTouch;  // Global — filled by parser

// Parse a 6-byte Synaptics absolute packet
void parseAbsolutePacket(uint8_t *buf) {
  TouchData &t = gTouch;

  // Validate sync bits
  if ((buf[0] & 0xC8) != 0x80 || (buf[3] & 0xC8) != 0xC0) {
    t.valid = false;
    return;
  }

  // Extract coordinates (13-bit)
  t.x = (uint16_t)(((buf[3] & 0x10) << 8) | ((buf[1] & 0x0F) << 8) | buf[4]);
  t.y = (uint16_t)(((buf[3] & 0x20) << 7) | ((buf[1] & 0xF0) << 4) | buf[5]);

  // Pressure
  t.z = buf[2];

  // W value (scattered across bytes 0 and 3)
  t.w = (uint8_t)(((buf[0] & 0x30) >> 2) | ((buf[0] & 0x04) >> 1) | ((buf[3] & 0x04) >> 2));

  // Buttons
  t.btnL = buf[0] & 0x01;
  t.btnR = buf[0] & 0x02;
  t.btnM = ((buf[0] ^ buf[3]) & 0x01);  // Middle = XOR of bit 0

  // Derive finger count and handle AGM (Advanced Gesture Mode)
  static uint16_t lastAgmX = 0, lastAgmY = 0;
  static uint8_t  lastAgmZ = 0;
  t.isAgm = false;

  if (t.z < FINGER_Z_OFF) {
    t.fingers = 0;
    t.x2 = 0; t.y2 = 0; t.z2 = 0;
  } else if (t.w == 2) {
    // W=2 is an AGM packet. It contains the SECOND finger's data.
    t.isAgm = true;
    lastAgmX = t.x;
    lastAgmY = t.y;
    lastAgmZ = t.z;
    // We don't process gestures on AGM packets alone, we wait for the primary packet
    t.fingers = 2; 
  } else {
    // Standard primary packet
    if (t.w >= 4) {
      t.fingers = 1;
      t.x2 = 0; t.y2 = 0; t.z2 = 0;
    } else if (t.w == 0 || t.w == 1) {
      t.fingers = (t.w == 0) ? 2 : 3;
      // If we recently got an AGM packet, attach the 2nd finger data
      t.x2 = lastAgmX;
      t.y2 = lastAgmY;
      t.z2 = lastAgmZ;
    } else {
      t.fingers = 1;
      t.x2 = 0; t.y2 = 0; t.z2 = 0;
    }
  }

  t.valid = true;
}


// ====================== GESTURE ENGINE =========================
// Absolute mode: computes deltas from position changes.
// Tracks finger count for scroll vs cursor.
// ===============================================================

// -- Gesture output (global to avoid Arduino auto-prototype issue) --
struct GestureOut {
  int16_t dx, dy;         // Cursor movement
  int8_t  scrollV;        // Vertical scroll (+ = up)
  int8_t  scrollH;        // Horizontal scroll (+ = right)
  uint8_t btnPress;       // Buttons to press
  uint8_t btnRelease;     // Buttons to release
  bool    tapClickL;      // Emit left tap-click
  bool    tapClickR;      // Emit right tap-click
  bool    tapRelease;     // Release tap click
  bool    zoomIn;         // Trigger zoom in
  bool    zoomOut;        // Trigger zoom out
  bool    hasAction;
};

static GestureOut gOut;

// -- Tap/Drag state --
enum TapPhase {
  TP_NONE,            // Idle
  TP_HOLD_R,          // Holding right-click (2F tap)
  TP_DRAG_DETECT,     // 1F tap detected, waiting for finger return to start drag
  TP_DRAGGING         // Drag in progress (LEFT held, cursor moving)
};

// -- Gesture tracking state --
static bool     gFingerWasDown     = false;
static uint8_t  gPrevFingers       = 0;
static uint16_t gPrevX             = 0;
static uint16_t gPrevY             = 0;
static bool     gHasPrevPos        = false;

// Tap tracking
static unsigned long gFingerDownMs = 0;
static int       gFingerMovement   = 0;
static uint8_t   gFingerDownCount  = 0;

// Tap/Drag click state
static TapPhase  gTapPhase         = TP_NONE;
static unsigned long gTapClickMs   = 0;

// Scroll accumulator
static int       gScrollAccumY     = 0;
static int       gScrollAccumX     = 0;

// Scroll velocity tracking (for momentum)
static float     gScrollVelY       = 0.0f;   // Smoothed scroll velocity
static float     gScrollVelX       = 0.0f;
static bool      gWasScrolling     = false;   // Were we scrolling last frame?

// Momentum state
static float     gMomentumY        = 0.0f;   // Current momentum velocity
static float     gMomentumX        = 0.0f;
static bool      gMomentumActive   = false;
static unsigned long gLastMomentumMs = 0;
static float     gMomentumAccumY   = 0.0f;   // Sub-line accumulator for momentum
static float     gMomentumAccumX   = 0.0f;

// Pinch to zoom state
static float     gPrevPinchDist    = 0.0f;
static float     gPinchAccum       = 0.0f;
#define PINCH_THRESHOLD            80.0f     // Distance change required to trigger a zoom step

// Physical button tracking
static uint8_t   gPrevBtnState     = 0;

// Process an absolute mode touch data packet
void gestureProcessAbs() {
  GestureOut &o = gOut;
  o = {0, 0, 0, 0, 0, 0, false, false, false, false, false, false};

  TouchData &t = gTouch;
  unsigned long now = millis();
  bool fingerDown = (t.z >= FINGER_Z_ON) || (gFingerWasDown && t.z >= FINGER_Z_OFF);

  // --- Physical button handling ---
  uint8_t curBtn = 0;
  if (t.btnL) curBtn |= MOUSE_LEFT;
  if (t.btnR) curBtn |= MOUSE_RIGHT;
  if (t.btnM) curBtn |= MOUSE_MIDDLE;
  o.btnPress   = curBtn & ~gPrevBtnState;
  o.btnRelease = ~curBtn & gPrevBtnState;
  gPrevBtnState = curBtn;
  if (o.btnPress || o.btnRelease) o.hasAction = true;
  
  // We don't process movement deltas during an AGM (second finger) packet,
  // otherwise it causes phantom jumps between finger 1 and finger 2.
  if (t.isAgm) return;

  // --- Finger state transitions ---
  if (fingerDown && !gFingerWasDown) {
    // FINGER LANDED
    gFingerDownMs    = now;
    gFingerMovement  = 0;
    gFingerDownCount = t.fingers;
    gHasPrevPos      = false;
    gScrollAccumX    = 0;
    gScrollAccumY    = 0;

    // Stop any active momentum
    gMomentumActive = false;
    gMomentumY = 0; gMomentumX = 0;
    gMomentumAccumY = 0; gMomentumAccumX = 0;

    // Check if we're in drag-detect and finger returned → start drag!
    if (gTapPhase == TP_DRAG_DETECT && t.fingers == 1) {
      gTapPhase = TP_DRAGGING;
      o.btnPress |= MOUSE_LEFT;  // Press LEFT for drag
      o.hasAction = true;
    }
  }

  if (!fingerDown && gFingerWasDown) {
    // FINGER LIFTED
    unsigned long duration = now - gFingerDownMs;

    if (gTapPhase == TP_DRAGGING) {
      // End drag
      o.btnRelease |= MOUSE_LEFT;
      o.hasAction = true;
      gTapPhase = TP_NONE;
    } else if (gFingerDownCount == 1 && duration < TAP_MAX_DURATION_MS && gFingerMovement < TAP_MAX_MOVEMENT) {
      // 1-finger tap → enter drag detection (wait for finger return)
      gTapPhase = TP_DRAG_DETECT;
      gTapClickMs = now;
      // Don't emit click yet — wait to see if drag starts
    } else if (gFingerDownCount == 2 && duration < TAP_2F_MAX_DURATION && gFingerMovement < TAP_2F_MAX_MOVEMENT) {
      // 2-finger tap → right click
      o.tapClickR = true;
      o.hasAction = true;
      gTapPhase   = TP_HOLD_R;
      gTapClickMs = now;
    }

    // --- Start momentum if we were scrolling ---
    if (gWasScrolling && (abs(gScrollVelY) > MOMENTUM_MIN_VEL || abs(gScrollVelX) > MOMENTUM_MIN_VEL)) {
      gMomentumY = gScrollVelY;
      gMomentumX = gScrollVelX;
      // Clamp
      if (gMomentumY > MOMENTUM_MAX_VEL) gMomentumY = MOMENTUM_MAX_VEL;
      if (gMomentumY < -MOMENTUM_MAX_VEL) gMomentumY = -MOMENTUM_MAX_VEL;
      if (gMomentumX > MOMENTUM_MAX_VEL) gMomentumX = MOMENTUM_MAX_VEL;
      if (gMomentumX < -MOMENTUM_MAX_VEL) gMomentumX = -MOMENTUM_MAX_VEL;
      gMomentumActive = true;
      gLastMomentumMs = now;
      gMomentumAccumY = 0;
      gMomentumAccumX = 0;
    }

    gHasPrevPos = false;
    gWasScrolling = false;
    gScrollVelY = 0; gScrollVelX = 0;
  }

  // --- Movement / scroll processing ---
  if (fingerDown) {
    // Track maximum finger count during this touch session
    // (fixes 2-finger tap when fingers don't land simultaneously)
    if (t.fingers > gFingerDownCount) gFingerDownCount = t.fingers;

    // Detect finger count change (e.g., 1→2 or 2→1)
    if (t.fingers != gPrevFingers && gPrevFingers != 0) {
      gHasPrevPos  = false;
      gScrollAccumX = 0;
      gScrollAccumY = 0;
      gPinchAccum   = 0;
      gScrollVelY = 0; gScrollVelX = 0;
      gWasScrolling = false;
    }

    if (gHasPrevPos) {
      int deltaX = (int)t.x - (int)gPrevX;
      int deltaY = (int)t.y - (int)gPrevY;

      gFingerMovement += abs(deltaX) + abs(deltaY);

      if (abs(deltaX) + abs(deltaY) > DEAD_ZONE) {

        if (t.fingers == 1) {
          // --- ONE FINGER: Cursor movement ---
          int mx = (int)(deltaX * SENSITIVITY_ABS);
          int my = (int)(-deltaY * SENSITIVITY_ABS);
          if (mx > 127) mx = 127; if (mx < -127) mx = -127;
          if (my > 127) my = 127; if (my < -127) my = -127;
          o.dx = (int16_t)mx;
          o.dy = (int16_t)my;
          if (o.dx != 0 || o.dy != 0) o.hasAction = true;

          gWasScrolling = false;

        } else if (t.fingers == 2) {
          // --- TWO FINGERS: Scroll & Pinch ---
          gWasScrolling = true;

          // Calculate distance between fingers for pinch-to-zoom
          if (t.x2 != 0 && t.y2 != 0) {
            float dx2 = (float)t.x - (float)t.x2;
            float dy2 = (float)t.y - (float)t.y2;
            float dist = sqrt(dx2*dx2 + dy2*dy2);
            
            if (gPrevPinchDist > 0) {
              float pinchDelta = dist - gPrevPinchDist;
              gPinchAccum += pinchDelta;
              
              if (abs(gPinchAccum) > PINCH_THRESHOLD) {
                 if (gPinchAccum > 0) {
                   o.zoomIn = true;
                 } else {
                   o.zoomOut = true;
                 }
                 o.hasAction = true;
                 gPinchAccum = 0; // Reset accumulator after triggering
              }
            }
            gPrevPinchDist = dist;
          } else {
            gPrevPinchDist = 0.0f;
          }

          // Update velocity tracking (exponential moving average)
          float rawVelY = (float)deltaY / (float)SCROLL_DIVISOR;
          float rawVelX = (float)deltaX / (float)SCROLL_H_DIVISOR;
          gScrollVelY = gScrollVelY * (1.0f - SCROLL_VELOCITY_SMOOTH) + rawVelY * SCROLL_VELOCITY_SMOOTH;
          gScrollVelX = gScrollVelX * (1.0f - SCROLL_VELOCITY_SMOOTH) + rawVelX * SCROLL_VELOCITY_SMOOTH;

          // Accumulate and emit scroll lines
          gScrollAccumY += deltaY;
          gScrollAccumX += deltaX;

          int scrollY = gScrollAccumY / SCROLL_DIVISOR;
          int scrollX = gScrollAccumX / SCROLL_H_DIVISOR;

          if (scrollY != 0) {
            gScrollAccumY -= scrollY * SCROLL_DIVISOR;
            if (SCROLL_INVERT_Y) scrollY = -scrollY;
            if (scrollY > 5) scrollY = 5; if (scrollY < -5) scrollY = -5;
            o.scrollV = (int8_t)scrollY;
            o.hasAction = true;
          }

          if (scrollX != 0) {
            gScrollAccumX -= scrollX * SCROLL_H_DIVISOR;
            if (SCROLL_INVERT_X) scrollX = -scrollX;
            if (scrollX > 5) scrollX = 5; if (scrollX < -5) scrollX = -5;
            o.scrollH = (int8_t)scrollX;
            o.hasAction = true;
          }
        }
      }
    }

    // Update tracking
    gPrevX = t.x;
    gPrevY = t.y;
    gHasPrevPos = true;
    gPrevFingers = t.fingers;
  } else {
    gPrevFingers = 0;
  }

  gFingerWasDown = fingerDown;
}

// Idle processing — handle tap/drag timing and momentum scrolling
void gestureIdleAbs() {
  GestureOut &o = gOut;
  o = {0, 0, 0, 0, 0, 0, false, false, false, false};
  unsigned long now = millis();

  // --- Tap/Drag state machine ---
  switch (gTapPhase) {
    case TP_DRAG_DETECT:
      // Waiting for finger to return for drag
      if ((now - gTapClickMs) > DRAG_DETECT_MS) {
        // Timeout — finger didn't come back. Emit normal tap click.
        o.tapClickL = true;
        o.hasAction = true;
        // Brief hold then release
        gTapPhase = TP_NONE;
        // We need to press and release quickly — press now
        // and set a timer to release. Use TP_HOLD_R repurposed... 
        // Actually, let's just do click+release in one shot:
        // bleMouse will handle it in sendBLEOutput
      }
      break;

    case TP_HOLD_R:
      // Release right-click after hold
      if ((now - gTapClickMs) > TAP_CLICK_HOLD_MS) {
        o.tapRelease = true;
        o.hasAction  = true;
        gTapPhase    = TP_NONE;
      }
      break;

    default:
      break;
  }

  // --- Momentum scrolling ---
  if (gMomentumActive) {
    if ((now - gLastMomentumMs) >= MOMENTUM_INTERVAL_MS) {
      gLastMomentumMs = now;

      // Decay velocity
      gMomentumY *= MOMENTUM_DECAY;
      gMomentumX *= MOMENTUM_DECAY;

      // Check if momentum should stop
      if (abs(gMomentumY) < MOMENTUM_MIN_VEL && abs(gMomentumX) < MOMENTUM_MIN_VEL) {
        gMomentumActive = false;
        gMomentumY = 0; gMomentumX = 0;
        gMomentumAccumY = 0; gMomentumAccumX = 0;
        return;
      }

      // Accumulate sub-line scroll
      gMomentumAccumY += gMomentumY;
      gMomentumAccumX += gMomentumX;

      int scrollY = (int)gMomentumAccumY;
      int scrollX = (int)gMomentumAccumX;

      if (scrollY != 0 || scrollX != 0) {
        gMomentumAccumY -= scrollY;
        gMomentumAccumX -= scrollX;

        if (SCROLL_INVERT_Y) scrollY = -scrollY;
        if (SCROLL_INVERT_X) scrollX = -scrollX;
        if (scrollY > 3) scrollY = 3; if (scrollY < -3) scrollY = -3;
        if (scrollX > 3) scrollX = 3; if (scrollX < -3) scrollX = -3;

        o.scrollV = (int8_t)scrollY;
        o.scrollH = (int8_t)scrollX;
        o.hasAction = true;
      }
    }
  }
}


// ============== PHASE 1 FALLBACK GESTURE ENGINE ================
// Standard 3-byte PS/2 mode (relative dX/dY, no finger count)
// ===============================================================

enum FbTapState { FB_IDLE, FB_TOUCHING, FB_MOVING, FB_CLICKING };

static FbTapState  fbTapState       = FB_IDLE;
static unsigned long fbTouchStart   = 0;
static unsigned long fbLastPacket   = 0;
static int         fbTouchMovement  = 0;
static uint8_t     fbPrevButtons    = 0;
static unsigned long fbClickStart   = 0;

void gestureProcessRel(uint8_t b0, uint8_t b1, uint8_t b2) {
  GestureOut &o = gOut;
  o = {0, 0, 0, 0, 0, 0, false, false, false, false};
  unsigned long now = millis();

  int dx = (int)b1; int dy = (int)b2;
  if (b0 & 0x10) dx -= 256;
  if (b0 & 0x20) dy -= 256;

  o.dx = (int16_t)(dx * SENSITIVITY_REL);
  o.dy = (int16_t)(-dy * SENSITIVITY_REL);

  uint8_t cur = b0 & 0x07;
  uint8_t pr = cur & ~fbPrevButtons;
  uint8_t rl = ~cur & fbPrevButtons;
  if (pr & 0x01) o.btnPress |= MOUSE_LEFT;
  if (pr & 0x02) o.btnPress |= MOUSE_RIGHT;
  if (pr & 0x04) o.btnPress |= MOUSE_MIDDLE;
  if (rl & 0x01) o.btnRelease |= MOUSE_LEFT;
  if (rl & 0x02) o.btnRelease |= MOUSE_RIGHT;
  if (rl & 0x04) o.btnRelease |= MOUSE_MIDDLE;
  fbPrevButtons = cur;

  fbLastPacket = now;
  switch (fbTapState) {
    case FB_IDLE:
      fbTapState = FB_TOUCHING; fbTouchStart = now; fbTouchMovement = abs(dx)+abs(dy); break;
    case FB_TOUCHING:
      fbTouchMovement += abs(dx)+abs(dy);
      if (fbTouchMovement > 20 || (now - fbTouchStart) > TAP_MAX_DURATION_MS) fbTapState = FB_MOVING;
      break;
    default: break;
  }

  o.hasAction = (o.dx!=0 || o.dy!=0 || o.btnPress || o.btnRelease);
}

void gestureIdleRel() {
  GestureOut &o = gOut;
  o = {0, 0, 0, 0, 0, 0, false, false, false, false};
  unsigned long now = millis();

  switch (fbTapState) {
    case FB_TOUCHING:
      if ((now - fbLastPacket) > 80) {
        if ((fbLastPacket - fbTouchStart) < TAP_MAX_DURATION_MS && fbTouchMovement < 20) {
          o.tapClickL = true; o.hasAction = true;
          fbTapState = FB_CLICKING; fbClickStart = now;
        } else { fbTapState = FB_IDLE; }
      }
      break;
    case FB_MOVING:
      if ((now - fbLastPacket) > 80) fbTapState = FB_IDLE;
      break;
    case FB_CLICKING:
      if ((now - fbClickStart) > TAP_CLICK_HOLD_MS) {
        o.tapRelease = true; o.hasAction = true; fbTapState = FB_IDLE;
      }
      break;
    default: break;
  }
}


// ======================== STATUS LED ============================

static unsigned long ledLastToggle = 0;
static bool ledState = false;

void ledSetup() { pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW); }

void ledUpdate(bool bleConn, bool ps2Ok) {
  unsigned long now = millis();
  if (!ps2Ok) {
    if (now - ledLastToggle > 100) { ledState = !ledState; digitalWrite(LED_PIN, ledState); ledLastToggle = now; }
  } else if (!bleConn) {
    if (now - ledLastToggle > 500) { ledState = !ledState; digitalWrite(LED_PIN, ledState); ledLastToggle = now; }
  } else {
    if (!ledState) { ledState = true; digitalWrite(LED_PIN, HIGH); }
  }
}


// ======================== GLOBAL STATE ==========================

static bool     ps2Initialized = false;
static bool     absoluteMode   = false;   // True if Synaptics absolute mode active
static uint32_t totalPackets   = 0;
static uint32_t totalErrors    = 0;
static unsigned long lastStatsMs = 0;

// Packet assembly
static int      pktIndex       = 0;
static uint8_t  pktBytes[6]    = {0};


// ======================== SETUP =================================

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("=============================================");
  Serial.println("  ESP32 BLE Trackpad — Phase 2");
  Serial.println("  Synaptics Absolute Mode + Gestures");
  Serial.println("  CLK: GPIO16   DATA: GPIO17");
  Serial.println("=============================================");
  Serial.println();

  ledSetup();

  // --- PS/2 Init ---
  Serial.println("[PS/2] Initializing...");
  ps2Init();

  // Let touchpad fully boot (BAT can take up to 2 seconds)
  Serial.println("  Waiting for touchpad boot (1.5s)...");
  delay(1500);

  bool clkOk = (digitalRead(PS2_CLK_PIN) == HIGH);
  bool datOk = (digitalRead(PS2_DATA_PIN) == HIGH);
  Serial.printf("  CLK idle: %s\n", clkOk ? "HIGH (OK)" : "LOW (FAIL)");
  Serial.printf("  DATA idle: %s\n", datOk ? "HIGH (OK)" : "LOW (FAIL)");

  // Drain any spontaneous BAT/ID bytes from power-up
  ps2DrainBus();
  ps2Initialized = true;

  // --- Skip RESET — touchpad auto-initializes on power-up ---
  // RESET is unreliable (slow BAT response, missed bytes). The touchpad
  // starts in standard PS/2 mode automatically.

  // --- Attempt Synaptics Absolute Mode (with retries) ---
  Serial.println();
  absoluteMode = false;

  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("[Synaptics] Attempt %d/3 to activate absolute mode...\n", attempt);
    synapticsSetMode(SYNAPTICS_MODE_BYTE);

    // Brief pause then flush
    delay(100);
    ps2DrainBus();

    // Start ISR to collect test packets
    ps2StartISR();
    Serial.println("  Collecting test packets (touch the pad)...");
    delay(1500);  // Wait for user to touch or for idle packets

    // Check if any packets match absolute format
    int absCount = 0, stdCount = 0, totalTest = 0;
    while (ps2Available() > 0 && totalTest < 30) {
      int b = ps2Read();
      if (b < 0) break;
      totalTest++;

      // Check if this byte looks like absolute byte 0: (val & 0xC8) == 0x80
      if ((b & 0xC8) == 0x80) absCount++;
      // Check if this byte looks like standard byte 0: (val & 0x08) == 0x08 and bit 7 could be anything
      if ((b & 0x08) && !((b & 0xC8) == 0x80) && !((b & 0xC8) == 0xC0)) stdCount++;
    }

    ps2StopISR();
    Serial.printf("  Test: %d bytes, %d look absolute, %d look standard\n", totalTest, absCount, stdCount);

    if (absCount >= 2) {
      absoluteMode = true;
      Serial.println("[Synaptics] Absolute mode CONFIRMED from packet format!");
      break;
    }

    if (totalTest > 0 && absCount == 0) {
      Serial.println("  Standard packets detected — retrying mode set...");
    } else if (totalTest == 0) {
      Serial.println("  No packets received — check wiring or touch the pad.");
    }

    delay(200);
  }

  if (!absoluteMode) {
    Serial.println("[Synaptics] Could not confirm absolute mode.");
    Serial.println("  Falling back to standard 3-byte PS/2 (Phase 1).");

    // Make absolutely sure reporting is enabled for fallback
    ps2Low(PS2_CLK_PIN); delay(100); ps2Release(PS2_CLK_PIN); delay(50);
    ps2DrainBus();
    for (int i = 0; i < 3; i++) {
      int ack = ps2Command(0xF4);  // Enable reporting
      Serial.printf("  Enable reporting attempt %d: ack=0x%02X\n", i+1, ack);
      if (ack == 0xFA) break;
      delay(100);
    }
  }

  // Flush and (re)start ISR
  ps2DrainBus();
  Serial.println("[PS/2] Starting interrupt-driven receiver...");
  ps2StartISR();

  // Start BLE
  Serial.println("[BLE] Starting Bluetooth LE Mouse...");
  bleMouse.begin();

  Serial.println();
  Serial.println("=============================================");
  Serial.printf("  Mode: %s\n", absoluteMode ? "ABSOLUTE (Phase 2)" : "STANDARD (Phase 1 fallback)");
  Serial.println("  Pair 'ESP32 Trackpad' on your device.");
  Serial.println("=============================================");
  Serial.println();

  lastStatsMs = millis();
}


// ======================== LOOP ==================================

// Sends BLE output from gOut
void sendBLEOutput() {
  if (!bleMouse.isConnected() || !gOut.hasAction) return;

  // Cursor movement + scroll
  if (gOut.dx != 0 || gOut.dy != 0 || gOut.scrollV != 0 || gOut.scrollH != 0) {
    bleMouse.move(gOut.dx, gOut.dy, gOut.scrollV, gOut.scrollH);
  }

  // Physical button presses
  if (gOut.btnPress)  bleMouse.press(gOut.btnPress);
  if (gOut.btnRelease) bleMouse.release(gOut.btnRelease);

  // Tap clicks
  if (gOut.tapClickL) {
    bleMouse.click(MOUSE_LEFT);
  }
  if (gOut.tapClickR) bleMouse.press(MOUSE_RIGHT);
  if (gOut.tapRelease) {
    bleMouse.release(MOUSE_RIGHT);
  }
  
  if (gOut.zoomIn) {
    Serial.println("  >> ZOOM IN TRIGGERED");
  }
  if (gOut.zoomOut) {
    Serial.println("  >> ZOOM OUT TRIGGERED");
  }
}


void loop() {
  bool bleConn = bleMouse.isConnected();
  ledUpdate(bleConn, ps2Initialized);

  bool hadPacket = false;

  if (absoluteMode) {
    // ========== ABSOLUTE MODE (6-byte packets) ==========
    while (ps2Available() > 0) {
      int b = ps2Read();
      if (b < 0) break;

      if (pktIndex == 0) {
        // Sync on byte 0: bit pattern 1_0_xx_0_xxx → (val & 0xC8) == 0x80
        if ((b & 0xC8) != 0x80) {
          totalErrors++;
          continue;  // Discard, look for valid byte 0
        }
        pktBytes[0] = (uint8_t)b;
        pktIndex = 1;
      } else if (pktIndex < 3) {
        pktBytes[pktIndex] = (uint8_t)b;
        pktIndex++;
      } else if (pktIndex == 3) {
        // Validate byte 3: (val & 0xC8) == 0xC0
        if ((b & 0xC8) != 0xC0) {
          // Sync error — check if this is actually a new byte 0
          totalErrors++;
          if ((b & 0xC8) == 0x80) {
            pktBytes[0] = (uint8_t)b;
            pktIndex = 1;
          } else {
            pktIndex = 0;
          }
          continue;
        }
        pktBytes[3] = (uint8_t)b;
        pktIndex = 4;
      } else if (pktIndex < 6) {
        pktBytes[pktIndex] = (uint8_t)b;
        pktIndex++;

        if (pktIndex == 6) {
          // Full 6-byte packet received
          pktIndex = 0;
          totalPackets++;
          hadPacket = true;

          parseAbsolutePacket(pktBytes);

          if (gTouch.valid) {
            gestureProcessAbs();
            sendBLEOutput();
          }
        }
      }
    }

    // Idle processing (tap release timing)
    if (!hadPacket) {
      gestureIdleAbs();
      sendBLEOutput();
    }

  } else {
    // ========== STANDARD MODE FALLBACK (3-byte packets) ==========
    while (ps2Available() > 0) {
      int b = ps2Read();
      if (b < 0) break;

      if (pktIndex == 0) {
        if (!(b & 0x08)) { totalErrors++; continue; }
        pktBytes[0] = (uint8_t)b;
        pktIndex = 1;
      } else if (pktIndex == 1) {
        pktBytes[1] = (uint8_t)b;
        pktIndex = 2;
      } else {
        pktBytes[2] = (uint8_t)b;
        pktIndex = 0;
        totalPackets++;
        hadPacket = true;

        gestureProcessRel(pktBytes[0], pktBytes[1], pktBytes[2]);
        sendBLEOutput();
      }
    }

    if (!hadPacket) {
      gestureIdleRel();
      sendBLEOutput();
    }
  }

  // --- Periodic debug stats ---
  if (millis() - lastStatsMs > 5000) {
    lastStatsMs = millis();
    if (absoluteMode && hadPacket) {
      Serial.printf("[Stats] BLE:%s  Pkts:%lu  Err:%lu  X=%d Y=%d Z=%d W=%d F=%d\n",
                    bleConn ? "OK" : "wait",
                    totalPackets, totalErrors,
                    gTouch.x, gTouch.y, gTouch.z, gTouch.w, gTouch.fingers);
    } else {
      Serial.printf("[Stats] BLE:%s  Pkts:%lu  Err:%lu  Mode:%s\n",
                    bleConn ? "OK" : "wait",
                    totalPackets, totalErrors,
                    absoluteMode ? "ABS" : "STD");
    }
  }

  delay(1);
}
