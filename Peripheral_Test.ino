#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- Pin Definitions ---
#define BUTTON_PIN 13 // Button to GND, internal pull-up enabled
#define LED1_PIN 12   // Blinking LED
#define LED2_PIN 11   // Button-controlled LED
#define BUZZER_PIN 8
#define POT_PIN A1
#define PHOTO_PIN A0

// --- LCD Setup (16x2 standard I2C LCD) ---
LiquidCrystal_I2C lcd(0x27, 16, 2); // Try 0x3F if 0x27 doesn't work

// --- Timing Variables ---
unsigned long previousMillis = 0;
const long interval = 1000; // 1 second

// --- Debounce Variables ---
int lastButtonReading = HIGH; // last raw reading (HIGH = unpressed)
int stableButtonState = HIGH; // debounced stable state
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; // 50 ms debounce period

void setup()
{
    // --- Serial Monitor ---
    Serial.begin(9600);
    Serial.println("Hardware Test Starting...");

    // --- Pin Modes ---
    pinMode(LED1_PIN, OUTPUT);
    pinMode(LED2_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP); // use internal pull-up resistor

    // --- LCD Initialization ---
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Hello World!");

    Serial.println("LCD initialized with 'Hello World!'");
}

void loop()
{
    // --- 1. Blink LED1 (D12) ---
    static unsigned long lastBlink = 0;
    static bool led1State = false;
    if (millis() - lastBlink >= 500)
    { // Toggle every 0.5s
        lastBlink = millis();
        led1State = !led1State;
        tone(BUZZER_PIN, 294, 300);
        digitalWrite(LED1_PIN, led1State);
    }

    // --- 2. Debounce Button and Control LED2 ---
    int rawReading = digitalRead(BUTTON_PIN);

    if (rawReading != lastButtonReading)
    {
        // reset debounce timer whenever reading changes
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay)
    {
        // if reading has been stable long enough, accept it as the actual state
        if (rawReading != stableButtonState)
        {
            stableButtonState = rawReading;
            // Button pressed = LOW → LED ON
            digitalWrite(LED2_PIN, (stableButtonState == LOW) ? HIGH : LOW);
        }
    }

    lastButtonReading = rawReading;

    // --- 3. Read Potentiometer and Photoresistor ---
    if (millis() - previousMillis >= interval)
    {
        previousMillis = millis();

        int potValue = analogRead(POT_PIN);
        int photoValue = analogRead(PHOTO_PIN);

        float potPercent = (potValue / 1023.0) * 100.0;
        float photoPercent = (photoValue / 1023.0) * 100.0;

        Serial.print("Potentiometer: ");
        Serial.print(potPercent, 1);
        Serial.print("% | Photoresistor: ");
        Serial.print(photoPercent, 1);
        Serial.println("%");
    }
}
