#include <Arduino.h>
#include <Adafruit_VL53L0X.h>
#include <LiquidCrystal.h>
#include <IRremote.hpp>

/* ---------------- Pins ---------------- */
#define BUTTON_PIN  33
#define WHITE_LED   5
#define GREEN_LED   18
#define IR_PIN      32

/* ---------------- LCD ---------------- */
LiquidCrystal lcd(13, 12, 14, 27, 26, 25);

/* ---------------- TOF ---------------- */
Adafruit_VL53L0X lox;

/* ---------------- Physics ---------------- */
const float G = 9.81;
const float M_TO_IN = 39.3701;

/* ---------------- Detection ---------------- */
const int READY_ZONE_MM  = 610;
const int MIN_AIRTIME_MS = 120;
const int STABLE_COUNT   = 2;

/* ---------------- States ---------------- */
enum State { IDLE, WAITING_FOR_PERSON, READY_TO_JUMP, IN_AIR, RESULT };
State state = IDLE;

/* ---------------- Timing ---------------- */
unsigned long takeoffTime = 0;
unsigned long landingTime = 0;

/* ---------------- Button ---------------- */
bool lastButton  = HIGH;
bool buttonArmed = true;

/* ---------------- IR ---------------- */
#define BUTTON_0 0xE916FF00

/* ---------------- Helpers ---------------- */
float readDistance() {
  VL53L0X_RangingMeasurementData_t m;
  lox.rangingTest(&m, false);
  if (m.RangeStatus == 4) return -1;
  return m.RangeMilliMeter;
}

bool footInZone(float d) {
  return (d > 0 && d <= READY_ZONE_MM);
}

bool footGone(float d) {
  return (d < 0 || d > READY_ZONE_MM + 150);
}

void show(const char* line1, const char* line2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void leds(bool white, bool green) {
  digitalWrite(WHITE_LED, white);
  digitalWrite(GREEN_LED, green);
}

bool buttonPressed() {
  bool current = digitalRead(BUTTON_PIN);
  bool pressed = false;
  if (buttonArmed && lastButton == HIGH && current == LOW) {
    pressed = true;
    buttonArmed = false;
  }
  if (current == HIGH) buttonArmed = true;
  lastButton = current;
  return pressed;
}

bool irPressed() {
  if (!IrReceiver.decode()) return false;
  uint32_t code = IrReceiver.decodedIRData.decodedRawData;
  IrReceiver.resume();
  if (code == 0x00000000 || code == 0xFFFFFFFF) return false;
  return (code == BUTTON_0);
}

/* ---------------- Setup ---------------- */
void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(WHITE_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  leds(false, false);

  lcd.begin(16, 2);

  Wire.begin(21, 22);

  if (!lox.begin()) {
    show("TOF FAIL", "Check wiring");
    while (1);
  }

  show("Press to begin", "");

  IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK);
}

/* ---------------- Loop ---------------- */
void loop() {
  bool triggered = buttonPressed() || irPressed();

  if (triggered && (state == IDLE || state == RESULT)) {
    state = WAITING_FOR_PERSON;
    leds(true, false);
    show("Waiting for", "person < 2ft");
  }

  float d = readDistance();
  static int stableCount = 0;

  switch (state) {

    case WAITING_FOR_PERSON:
      if (footInZone(d)) {
        stableCount++;
        if (stableCount >= STABLE_COUNT) {
          state = READY_TO_JUMP;
          stableCount = 0;
          leds(false, true);
          show("Ready to jump", "");
        }
      } else {
        stableCount = 0;
      }
      break;

    case READY_TO_JUMP:
      if (footGone(d)) {
        stableCount++;
        if (stableCount >= STABLE_COUNT) {
          state = IN_AIR;
          takeoffTime = millis();
          stableCount = 0;
          leds(false, true);
          show("Jump in progress", "");
        }
      } else {
        stableCount = 0;
      }
      break;

    case IN_AIR:
      if (footInZone(d)) {
        stableCount++;
        if (stableCount >= STABLE_COUNT) {
          landingTime = millis();
          unsigned long dt = landingTime - takeoffTime;
          leds(false, false);

          if (dt >= MIN_AIRTIME_MS) {
            float t = dt / 1000.0;
            float h = (G * t * t) / 8.0 * M_TO_IN;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Height: ");
            lcd.print(h, 1);
            lcd.print(" in");
            lcd.setCursor(0, 1);
            lcd.print("AirTime:");
            lcd.print(t, 2);
            lcd.print("s");
          } else {
            show("Jump too short", "Try again");
          }

          state = RESULT;
          stableCount = 0;
        }
      } else {
        stableCount = 0;
      }
      break;

    case RESULT:
    case IDLE:
      break;
  }

  delay(20);
}