/**
 * Hydration Unit - Hardware Verification
 * ======================================
 * A systematic test suite for the Hydration Slave PCB.
 *
 * Hardware Map:
 * - White LED: D25
 * - Buzzer:    D26
 * - RGB LED:   D27(R), D14(G), D12(B) [Common Anode]
 * - Button:    D13
 * - Scale:     D32(DT), D33(SCK)
 */

#include "HX711.h"

// --- Pin Definitions ---
const int PIN_LED_WHITE = 25;
const int PIN_BUZZER = 26;
const int PIN_RGB_R = 27;
const int PIN_RGB_G = 14;
const int PIN_RGB_B = 12;
const int PIN_BUTTON = 13;
const int PIN_SCALE_DT = 32;
const int PIN_SCALE_SCK = 33;

HX711 scale;

void setup() {
  Serial.begin(115200);
  while (!Serial)
    delay(10);

  // Pin Modes
  pinMode(PIN_LED_WHITE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RGB_R, OUTPUT);
  pinMode(PIN_RGB_G, OUTPUT);
  pinMode(PIN_RGB_B, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Init Scale
  scale.begin(PIN_SCALE_DT, PIN_SCALE_SCK);

  // Initial State: All OFF
  stopAll();

  printHeader();
  printMenu();
}

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    // Skip newlines/whitespace
    if (cmd == '\r' || cmd == '\n' || cmd == ' ')
      return;

    executeTest(cmd);
    printMenu();
  }
}

// --- Test Logic ---

void executeTest(char cmd) {
  Serial.print("\n>>> Running Test [");
  Serial.print(cmd);
  Serial.println("]... ");

  switch (cmd) {
  case '1':
    testWhiteLED();
    break;
  case '2':
    testBuzzer();
    break;
  case '3':
    testRGBPrimary();
    break;
  case '4':
    testRGBRainbow();
    break;
  case '5':
    testButton();
    break;
  case '6':
    testScale();
    break;
  case '9':
    testSystem();
    break;
  case '0':
    stopAll();
    Serial.println("All Systems OFF.");
    break;
  default:
    Serial.println("Invalid Selection.");
    break;
  }
  Serial.println(">>> Test Complete.\n");
}

void testWhiteLED() {
  Serial.println("  Blinking White LED (D25)...");
  for (int i = 0; i < 5; i++) {
    digitalWrite(PIN_LED_WHITE, HIGH);
    delay(100);
    digitalWrite(PIN_LED_WHITE, LOW);
    delay(100);
  }
}

void testBuzzer() {
  Serial.println("  Testing Buzzer (D26)...");
  // Simple Chirp
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(50);
    digitalWrite(PIN_BUZZER, LOW);
    delay(100);
  }
}

// Common Anode: LOW = ON, HIGH = OFF (255 = OFF, 0 = ON)
void setRGB(int r, int g, int b) {
  analogWrite(PIN_RGB_R, 255 - r);
  analogWrite(PIN_RGB_G, 255 - g);
  analogWrite(PIN_RGB_B, 255 - b);
}

void testRGBPrimary() {
  Serial.println("  Red...");
  setRGB(255, 0, 0);
  delay(500);
  Serial.println("  Green...");
  setRGB(0, 255, 0);
  delay(500);
  Serial.println("  Blue...");
  setRGB(0, 0, 255);
  delay(500);
  setRGB(0, 0, 0); // Off
}

void testRGBRainbow() {
  Serial.println("  Rainbow Cycle...");
  for (int j = 0; j < 256; j += 2) { // Fade Hue
    // Simple Hue conversion logic or just distinct colors
    if (j < 85) {
      setRGB(j * 3, 255 - j * 3, 0);
    } else if (j < 170) {
      setRGB(255 - (j - 85) * 3, 0, (j - 85) * 3);
    } else {
      setRGB(0, (j - 170) * 3, 255 - (j - 170) * 3);
    }
    delay(10);
  }
  setRGB(0, 0, 0);
}

void testButton() {
  Serial.println("  Please PRESS the button (D13) within 5 seconds...");
  unsigned long start = millis();
  bool pressed = false;
  while (millis() - start < 5000) {
    if (digitalRead(PIN_BUTTON) == LOW) {
      pressed = true;
      break;
    }
  }

  if (pressed) {
    Serial.println("  [PASS] Button Press Detected!");
    // Visual confirmation
    digitalWrite(PIN_LED_WHITE, HIGH);
    delay(500);
    digitalWrite(PIN_LED_WHITE, LOW);
  } else {
    Serial.println("  [FAIL] No press detected.");
  }
}

void testScale() {
  Serial.println("  Checking HX711 (DT:32, SCK:33)...");
  if (scale.is_ready()) {
    Serial.println("  [PASS] HX711 is ready.");
    Serial.println("  Reading raw values (10 samples)...");
    long sum = 0;
    for (int i = 0; i < 10; i++) {
      long val = scale.read();
      Serial.print("    Reading ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.println(val);
      sum += val;
      delay(100);
    }
    Serial.print("  Avg Raw: ");
    Serial.println(sum / 10);
  } else {
    Serial.println("  [FAIL] HX711 not ready. Check wiring.");
  }
}

void testSystem() {
  Serial.println("  Full System Check...");
  testWhiteLED();
  testBuzzer();
  testRGBPrimary();
  testScale();
}

void stopAll() {
  digitalWrite(PIN_LED_WHITE, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  setRGB(0, 0, 0);
}

// --- UI Helpers ---

void printHeader() {
  Serial.println("\n");
  Serial.println("***********************************");
  Serial.println("*    Hydration Hardware Test      *");
  Serial.println("***********************************");
}

void printMenu() {
  Serial.println("\nMENU:");
  Serial.println(" [1] Test White LED");
  Serial.println(" [2] Test Buzzer");
  Serial.println(" [3] Test RGB (Primary)");
  Serial.println(" [4] Test RGB (Rainbow)");
  Serial.println(" [5] Test Button");
  Serial.println(" [6] Test Scale (HX711)");
  Serial.println(" [9] Test EVERYTHING");
  Serial.println(" [0] Stop All");
  Serial.print("Select > ");
}
