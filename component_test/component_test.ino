/*
 * Hardware Component Test - Interactive
 * =====================================
 * Enter a number in the Serial Monitor to test specific components.
 *
 * Menu:
 * [1] White LED (GPIO 25)
 * [2] Buzzer (GPIO 26)
 * [3] RGB Red (GPIO 27)
 * [4] RGB Green (GPIO 14)
 * [5] RGB Blue (GPIO 12)
 * [6] RGB Cycle (All colors)
 * [7] Button (GPIO 13)
 * [8] Weight Scale (HX711 - GPIO 32, 33)
 * [9] All components (System Test)
 * [0] Reset / Turn off all
 */

#include "HX711.h"

// Pin definitions
#define LED_PIN 25
#define BUZZER_PIN 26
#define RGB_RED 27
#define RGB_GREEN 14
#define RGB_BLUE 12
#define BUTTON_PIN 13
#define HX711_DOUT 32
#define HX711_SCK 33

HX711 scale;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=====================================");
  Serial.println("   Interactive Hardware Test");
  Serial.println("=====================================");

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RGB_RED, OUTPUT);
  pinMode(RGB_GREEN, OUTPUT);
  pinMode(RGB_BLUE, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  scale.begin(HX711_DOUT, HX711_SCK);

  turnOffAll();
  printMenu();
}

void loop() {






    testWhiteLED();
  if (Serial.available() > 0) {
    char input = Serial.read();

    // Ignore newline/carriage return
    if (input == '\n' || input == '\r')
      return;

    Serial.print("\nExecuting Test [");
    Serial.print(input);
    Serial.println("]...");

    switch (input) {
    case '1':
      testWhiteLED();
      break;
    case '2':
      testBuzzer();
      break;
    case '3':
      testRGBColor(255, 0, 0, "RED");
      break;
    case '4':
      testRGBColor(0, 255, 0, "GREEN");
      break;
    case '5':
      testRGBColor(0, 0, 255, "BLUE");
      break;
    case '6':
      testRGBCycle();
      break;
    case '7':
      testButton();
      break;
    case '8':
      testScale();
      break;
    case '9':
      testAll();
      break;
    case '0':
      turnOffAll();
      Serial.println("All components OFF");
      break;
    default:
      Serial.println("Invalid option!");
      break;
    }

    delay(500);
    printMenu();
  }
}

void printMenu() {
  Serial.println("\n-------------------------------------");
  Serial.println("MENU - Enter index number:");
  Serial.println("[1] White LED (D25)");
  Serial.println("[2] Buzzer (D26)");
  Serial.println("[3] RGB Red (D27)");
  Serial.println("[4] RGB Green (D14)");
  Serial.println("[5] RGB Blue (D12)");
  Serial.println("[6] RGB Color Cycle");
  Serial.println("[7] Button Check (D13)");
  Serial.println("[8] Scale Weight Check");
  Serial.println("[9] All Components Together");
  Serial.println("[0] Turn Off Everything");
  Serial.println("-------------------------------------");
}

void turnOffAll() {
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  analogWrite(RGB_RED, 255);
  analogWrite(RGB_GREEN, 255);
  analogWrite(RGB_BLUE, 255);
}

void testWhiteLED() {
  Serial.println("Pulsing White LED 3 times...");
  for (int i = 0; i < 10; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(500);
 
    delay(500);
  }
     digitalWrite(LED_PIN, LOW);
}

void testBuzzer() {
  Serial.println("Beeping buzzer 3 times...");
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);
  }
}

void testRGBColor(int r, int g, int b, String name) {
  Serial.print("Setting RGB to ");
  Serial.println(name);
  analogWrite(RGB_RED, 255 - r);
  analogWrite(RGB_GREEN, 255 - g);
  analogWrite(RGB_BLUE, 255 - b);
  delay(2000);
  turnOffAll();
}

void testRGBCycle() {
  testRGBColor(255, 0, 0, "Red");
  testRGBColor(0, 255, 0, "Green");
  testRGBColor(0, 0, 255, "Blue");
  testRGBColor(255, 255, 255, "White");
}

void testButton() {
  Serial.println("Press the button now (GPIO 13)...");
  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      Serial.println("✓ BUTTON PRESSED!");
      digitalWrite(LED_PIN, HIGH);
      delay(500);
      digitalWrite(LED_PIN, LOW);
      return;
    }
    delay(10);
  }
  Serial.println("No press detected (5s timeout)");
}

void testScale() {
  Serial.println("Checking HX711...");
  if (scale.is_ready()) {
    Serial.println("HX711 found. Reading for 5 seconds...");
    unsigned long start = millis();
    while (millis() - start < 5000) {
      long value = scale.read();
      Serial.print("Raw Value: ");
      Serial.println(value);
      delay(500);
    }
  } else {
    Serial.println("HX711 not found! Check wiring (D32, D33)");
  }
}

void testAll() {
  Serial.println("Activating ALL components simultaneously!");
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  analogWrite(RGB_RED, 0); // Full White (Common Anode)
  analogWrite(RGB_GREEN, 0);
  analogWrite(RGB_BLUE, 0);
  delay(3000);
  turnOffAll();
  Serial.println("System test done.");
}
