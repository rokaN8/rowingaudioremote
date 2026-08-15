#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

// ESP32-C3 BLE media remote — native NimBLE-Arduino (2.x) HID, no BleKeyboard lib.
// 2 buttons, 2 LEDs, 2 modes:
//   Mode 1: button GPIO7 = previous track, button GPIO8 = next track
//   Mode 2: button GPIO7 = volume down,    button GPIO8 = volume up
// Hold both buttons 1s to toggle mode.
// LEDs:
//   BLE connect  -> both LEDs flash twice
//   Mode 1       -> LED GPIO20 flashes twice
//   Mode 2       -> LED GPIO21 flashes twice
//   Button held  -> its LED lit while down (GPIO7 -> LED21, GPIO8 -> LED20)
//
// Wiring:
//   Buttons: GPIO -> button -> GND (or 3V3 if active-high). See ACTIVE flags.
//   LEDs:    GPIO -> resistor (~220-330ohm) -> LED anode, cathode to GND.

// ---------------- CONFIG ----------------
const int BUTTON_PINS[] = {7, 8};
const int LED_PINS[]    = {21, 20};   // LED_PINS[i] pairs with BUTTON_PINS[i]

const bool BUTTONS_ACTIVE_LOW = true;  // true = press reads LOW (internal pullup)
const bool LEDS_ACTIVE_HIGH   = true;   // true = HIGH turns LED on

const unsigned long DEBOUNCE_MS   = 30;
const unsigned long MODE_HOLD_MS  = 1000;  // hold both buttons this long to switch mode
const unsigned long SERIAL_BAUD   = 115200;
const char*         DEVICE_NAME   = "Rowing Remote v2";
// ----------------------------------------

const int NUM_BUTTONS = sizeof(BUTTON_PINS) / sizeof(BUTTON_PINS[0]);

int  lastReading[NUM_BUTTONS];
int  stableState[NUM_BUTTONS];
unsigned long lastChangeMs[NUM_BUTTONS];

int  mode = 0;                      // 0 = prev/next, 1 = vol up/down
unsigned long bothHeldSince = 0;    // 0 = combo timer not running
bool comboFired = false;            // suppress release keys after a mode switch
bool connected    = false;
bool wasConnected = false;

// HID consumer-control usage IDs
const uint16_t MEDIA_NEXT     = 0x00B5;
const uint16_t MEDIA_PREV     = 0x00B6;
const uint16_t MEDIA_VOL_UP   = 0x00E9;
const uint16_t MEDIA_VOL_DOWN = 0x00EA;

const uint16_t MODE_KEYS[2][2] = {
  { MEDIA_PREV,   MEDIA_NEXT },
  { MEDIA_VOL_DOWN, MEDIA_VOL_UP },
};

// Consumer control: report ID 1, one 16-bit usage per report
static const uint8_t REPORT_MAP[] = {
  0x05, 0x0C,        // Usage Page (Consumer)
  0x09, 0x01,        // Usage (Consumer Control)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x15, 0x00,        //   Logical Minimum (0)
  0x26, 0xFF, 0x03,  //   Logical Maximum (0x3FF)
  0x19, 0x00,        //   Usage Minimum (0)
  0x2A, 0xFF, 0x03,  //   Usage Maximum (0x3FF)
  0x75, 0x10,        //   Report Size (16)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x00,        //   Input (Data, Array)
  0xC0               // End Collection
};

NimBLEHIDDevice*      hid;
NimBLECharacteristic* mediaInput;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override { connected = true; }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    connected = false;
    Serial.printf("BLE disconnected (reason %d)\n", reason);
  }
} serverCallbacks;

inline int pressedLevel() { return BUTTONS_ACTIVE_LOW ? LOW : HIGH; }
inline bool isPressed(int i) { return stableState[i] == pressedLevel(); }

void setLed(int idx, bool on) {
  digitalWrite(LED_PINS[idx], (on == LEDS_ACTIVE_HIGH) ? HIGH : LOW);
}

// ponytail: blocking flash (~600ms, buttons ignored meanwhile); go non-blocking if it ever matters
void flashLeds(bool led0, bool led1) {
  for (int n = 0; n < 2; n++) {
    setLed(0, led0);
    setLed(1, led1);
    delay(150);
    setLed(0, false);
    setLed(1, false);
    delay(150);
  }
}

void sendMediaKey(uint16_t usage) {
  mediaInput->setValue((uint8_t*)&usage, sizeof(usage));
  mediaInput->notify();
  uint16_t release = 0;
  mediaInput->setValue((uint8_t*)&release, sizeof(release));
  mediaInput->notify();
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println("NimBLE media remote starting");

  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(BUTTON_PINS[i], BUTTONS_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    lastReading[i] = digitalRead(BUTTON_PINS[i]);
    stableState[i] = lastReading[i];
    lastChangeMs[i] = 0;

    pinMode(LED_PINS[i], OUTPUT);
    setLed(i, false);
  }

  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setSecurityAuth(true, false, true);  // bonding, no MITM, secure connections

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  server->advertiseOnDisconnect(true);

  hid = new NimBLEHIDDevice(server);
  hid->setManufacturer("DIY");
  hid->setPnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  mediaInput = hid->getInputReport(1);

  server->start();  // NimBLE 2.x: this also starts the HID services

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName(DEVICE_NAME);
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(hid->getHidService()->getUUID());
  adv->start();

  Serial.println("Advertising");
}

void loop() {
  unsigned long now = millis();

  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected) {
      Serial.println("BLE connected");
      flashLeds(true, true);
    }
  }

  // ---- debounce + release-edge actions ----
  for (int i = 0; i < NUM_BUTTONS; i++) {
    int reading = digitalRead(BUTTON_PINS[i]);

    if (reading != lastReading[i]) {
      lastChangeMs[i] = now;
      lastReading[i] = reading;
    }

    if ((now - lastChangeMs[i]) > DEBOUNCE_MS && reading != stableState[i]) {
      bool wasDown = isPressed(i);
      stableState[i] = reading;

      // key fires on release so the 2s dual-hold combo doesn't also send keys
      if (wasDown && !isPressed(i) && !comboFired && connected) {
        sendMediaKey(MODE_KEYS[mode][i]);
        Serial.printf("GPIO %d -> media key (mode %d)\n", BUTTON_PINS[i], mode + 1);
      }
    }
  }

  // ---- dual-hold mode switch ----
  if (isPressed(0) && isPressed(1)) {
    if (bothHeldSince == 0) bothHeldSince = now;
    if (!comboFired && now - bothHeldSince >= MODE_HOLD_MS) {
      comboFired = true;
      mode = 1 - mode;
      Serial.printf("Mode -> %d\n", mode + 1);
      // mode 1 -> LED GPIO4 (idx 1), mode 2 -> LED GPIO7 (idx 0)
      flashLeds(mode == 1, mode == 0);
    }
  } else {
    bothHeldSince = 0;
    if (!isPressed(0) && !isPressed(1)) comboFired = false;
  }

  // ---- LED follows its button while held ----
  for (int i = 0; i < NUM_BUTTONS; i++) setLed(i, isPressed(i));
}
