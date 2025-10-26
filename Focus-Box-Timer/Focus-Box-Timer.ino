#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- Pins (same as your test) ----------------
#define BUTTON_PIN 13  // Button to GND, internal pull-up
#define LED1_PIN   12  // Status (focus/armed/celebrate)
#define LED2_PIN   11  // Shame / alert LED
#define BUZZER_PIN 8
#define POT_PIN    A1
#define PHOTO_PIN  A0

// ---------------- LCD ----------------
LiquidCrystal_I2C lcd(0x27, 16, 2); // Use 0x3F if 0x27 doesn't work

// ---------------- State machine ----------------
enum State { IDLE, SET_TIME, ARMED_WAIT_CLOSE, COUNTDOWN, SHAME, CELEBRATE };
State state = IDLE;

void setState(State s);

// ---------------- Timing / config ----------------
const unsigned MIN_MINUTES      = 5;
const unsigned MAX_MINUTES      = 60;
const unsigned POT_SNAP_STEP    = 5;        // choose in 5-min steps
const unsigned DARK_STABLE_MS   = 2000;     // must be dark this long to start
int  LIGHT_THRESHOLD            = 500;      // set after quick calibration
const int  LIGHT_HYST           = 30;       // hysteresis around threshold

// ---------------- Runtime vars ----------------
unsigned selMinutes = 25;
unsigned long targetMs = 0;
unsigned long startMs  = 0;
unsigned long savedRemaining = 0;
unsigned long enteredAt = 0;
unsigned long darkSince = 0;
bool          wasBright = false;

// ---------------- Button debounce (kept from your test) ----------------
int lastButtonReading = HIGH;
int stableButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ---------------- Helpers ----------------
void buz(unsigned f=1400, unsigned d=80) { tone(BUZZER_PIN, f, d); }

// (Definition can stay here or lower in the file; prototype above makes it safe)
void setState(State s) { 
  state = s; 
  enteredAt = millis(); 
  lcd.clear(); 
}

bool isBright(int raw) {
  int th = LIGHT_THRESHOLD + (wasBright ? -LIGHT_HYST : LIGHT_HYST);
  bool b = raw > th; wasBright = b; return b;
}

unsigned snapToStep(unsigned v, unsigned step) {
  return (unsigned)(( (v + step/2) / step ) * step);
}

unsigned readPotMinutes() {
  int raw = analogRead(POT_PIN);      // 0..1023
  float t = raw / 1023.0f;            // 0..1
  float m = MIN_MINUTES + t * (MAX_MINUTES - MIN_MINUTES);
  unsigned mins = (unsigned)round(m);
  mins = constrain(mins, MIN_MINUTES, MAX_MINUTES);
  mins = snapToStep(mins, POT_SNAP_STEP);
  return mins;
}

void printCentered(const char* l1, const char* l2) {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
}

void printTimeLeft(unsigned long msLeft) {
  unsigned long s = (msLeft + 999)/1000;
  unsigned m = s/60, r = s%60;

  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "FOCUS");
  snprintf(line2, sizeof(line2), "Time %02u:%02u", m, r);
  lcd.setCursor(0,0); lcd.print(line1); lcd.print("           ");
  lcd.setCursor(0,1); lcd.print(line2); lcd.print("     ");
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(9600);

  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  printCentered("FOCUS BOX v1", "Press to start");

  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);

  buz(1200,60);
}

// ---------------- Loop ----------------
void loop() {
  unsigned long now = millis();

  // ----- Debounce button (same pattern as your test) -----
  int rawReading = digitalRead(BUTTON_PIN);
  if (rawReading != lastButtonReading) lastDebounceTime = now;

  bool buttonEdgePressed = false; // true on clean HIGH->LOW (press)
  if ((now - lastDebounceTime) > debounceDelay) {
    if (rawReading != stableButtonState) {
      // stable change detected
      buttonEdgePressed = (stableButtonState == HIGH && rawReading == LOW);
      stableButtonState = rawReading;
    }
  }
  lastButtonReading = rawReading;

  // ----- Sensors -----
  int ldrRaw = analogRead(PHOTO_PIN);
  bool bright = isBright(ldrRaw);

  // ----- State machine -----
  switch (state) {
    case IDLE: {
      // Show hint + default time
      lcd.setCursor(0,0); lcd.print("Press to start  ");
      lcd.setCursor(0,1); lcd.print("Default 25:00   ");
      digitalWrite(LED1_PIN, LOW);
      digitalWrite(LED2_PIN, LOW);

      if (buttonEdgePressed) {
        selMinutes = 25; // default
        setState(SET_TIME);
        buz(1400,60);
        // brief status
        lcd.setCursor(0,0); lcd.print("Set with knob   ");
        lcd.setCursor(0,1); lcd.print("Press to lockin");
      }
      break;
    }

    case SET_TIME: {
      selMinutes = readPotMinutes();

      // Display selected minutes as MM:00
      lcd.setCursor(0,0); lcd.print("Set:            ");
      char buf[17];
      snprintf(buf, sizeof(buf), "Len %02u:00        ", selMinutes);
      lcd.setCursor(0,1); lcd.print(buf);

      // slow blink LED1 to show "setting" mode
      digitalWrite(LED1_PIN, ((now/400)%2)==0 ? HIGH : LOW);
      digitalWrite(LED2_PIN, LOW);

      if (buttonEdgePressed) {
        targetMs = selMinutes * 60UL * 1000UL;
        setState(ARMED_WAIT_CLOSE);
        darkSince = 0;
        buz(1000,70);
        printCentered("Close the box", "to arm timer");
        digitalWrite(LED1_PIN, LOW);
      }
      break;
    }

    case ARMED_WAIT_CLOSE: {
      // Show chosen time while waiting
      unsigned long s = targetMs/1000UL;
      unsigned m = s/60;
      char line2[17];
      snprintf(line2, sizeof(line2), "Target %02u:00     ", m);
      lcd.setCursor(0,0); lcd.print("Waiting darkness");
      lcd.setCursor(0,1); lcd.print(line2);

      // LED1 slow blink = armed, LED2 off
      digitalWrite(LED1_PIN, ((now/600)%2)==0 ? HIGH : LOW);
      digitalWrite(LED2_PIN, LOW);

      if (!bright) {
        if (darkSince == 0) darkSince = now;
        if (now - darkSince >= DARK_STABLE_MS) {
          setState(COUNTDOWN);
          startMs = now;
          savedRemaining = targetMs;
          buz(1500,60);
          digitalWrite(LED1_PIN, HIGH); // solid during focus
        }
      } else {
        darkSince = 0;
      }

      // Cancel back to idle on press
      if (buttonEdgePressed) {
        setState(IDLE);
        buz(900,50);
        printCentered("Canceled", "Press to start");
      }
      break;
    }

    case COUNTDOWN: {
      unsigned long elapsed = now - startMs;
      unsigned long remain  = (savedRemaining > elapsed) ? (savedRemaining - elapsed) : 0;

      printTimeLeft(remain);
      digitalWrite(LED1_PIN, HIGH); // solid = focusing
      digitalWrite(LED2_PIN, LOW);

      if (bright) {
        // freeze timer, go to shame
        savedRemaining = remain;
        setState(SHAME);
        buz(2000,140);
        digitalWrite(LED1_PIN, LOW);
        digitalWrite(LED2_PIN, HIGH);
        break;
      }
      if (remain == 0) {
        setState(CELEBRATE);
        for (int i=0;i<3;i++){ buz(1200,120); delay(140); }
        digitalWrite(LED1_PIN, HIGH);
        digitalWrite(LED2_PIN, LOW);
      }
      break;
    }

    case SHAME: {
      // Flash red LED and angry message; require lid closed + press to re-arm
      bool flash = ((now/200)%2)==0;
      digitalWrite(LED2_PIN, flash ? HIGH : LOW);
      lcd.setCursor(0,0); lcd.print("UNEMPLOYMENT... ");
      lcd.setCursor(0,1); lcd.print("Close+Press     ");
      if (flash) buz(2200,40);

      if (!bright && buttonEdgePressed) {
        setState(ARMED_WAIT_CLOSE);
        darkSince = 0;
        buz(1100,60);
        printCentered("Close the box", "to re-arm");
        digitalWrite(LED2_PIN, LOW);
      }
      break;
    }

    case CELEBRATE: {
      // Fun blink + message; press to reset
      bool flash = ((now/250)%2)==0;
      digitalWrite(LED1_PIN, flash ? HIGH : LOW);
      lcd.setCursor(0,0); lcd.print("Money & Funny UP");
      lcd.setCursor(0,1); lcd.print("Press to reset  ");
      if (buttonEdgePressed) {
        setState(IDLE);
        buz(1000,60);
        printCentered("Press to start", "Default 25:00");
        digitalWrite(LED1_PIN, LOW);
      }
      break;
    }
  }

  // ---- Periodic debug for calibration ----
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    int potValue = analogRead(POT_PIN);
    int photoValue = analogRead(PHOTO_PIN);
    float potPercent = (potValue / 1023.0) * 100.0;
    float photoPercent = (photoValue / 1023.0) * 100.0;
    Serial.print("State="); Serial.print(state);
    Serial.print(" | Pot="); Serial.print(potPercent,1); Serial.print("%");
    Serial.print(" | LDR="); Serial.print(photoPercent,1); Serial.print("%");
    Serial.print(" | RawLDR="); Serial.print(photoValue);
    Serial.print(" | Thr="); Serial.print(LIGHT_THRESHOLD);
    Serial.println();
  }
}
