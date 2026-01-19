/*
 * ESP8266 IR Receiver & Transmitter (Stubbed)
 *
 * Hardware Requirements:
 * - NodeMCU ESP8266
 * - IR Receiver Module (VS1838B or similar)
 *
 * Wiring:
 * - IR Receiver Signal -> GPIO 14 (D5)
 * - IR Receiver VCC    -> 3.3V
 * - IR Receiver GND    -> G
 *
 * Note: Transmitter pin is defined as GPIO 4 (D2) but is optional for this
 * receiver-only test.
 */

#include <Arduino.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>

// Pin Definitions
const uint16_t IR_RECV_PIN = 14; // D5
const uint16_t IR_SEND_PIN = 4;  // D2 - Optional if only receiving

// Create Objects
// Increase buffer to 1024, timeout 50ms, save buffer true
IRrecv irReceiver(IR_RECV_PIN, 1024, 50, true);
IRsend irSender(IR_SEND_PIN);
decode_results results;

// Global State
bool transmitMode = false;
uint64_t lastReceivedCode = 0;
decode_type_t lastProtocol = UNKNOWN;
uint16_t lastBits = 0;
uint16_t lastRawLen = 0;
uint16_t lastRawData[256];

void setup() {
  // Initialize Serial
  Serial.begin(9600);
  while (!Serial)
    delay(50);

  // Initialize IR Hardware
  irReceiver.enableIRIn();
  irSender.begin();

  // Print Welcome Message
  Serial.println();
  Serial.println("=============================");
  Serial.println("ESP8266 IR Receiver");
  Serial.println("=============================");
  Serial.println("Ready to receive signals.");
  Serial.println("Connect IR Receiver to Pin D5 (GPIO 14).");
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  t - Simulates transmitting the last received code");
  Serial.println("=============================");
}

void loop() {
  // Check for incoming Serial commands (to start transmit or stop loop)
  handleSerialCommands();

  if (transmitMode) {
    // Transmit loop
    Serial.print("TX: 0x");
    serialPrintUint64(lastReceivedCode, HEX);
    Serial.println();

    // Send the code (Defaulting to NEC 32-bit for manual input)
    // Note: If you want to support other protocols manually, you'd need more
    // complex parsing
    irSender.sendNEC(lastReceivedCode, 32);

    // Delay between transmits
    delay(500); // 500ms delay
  } else {
    // Receive mode
    receiveIR();
  }
}

void handleSerialCommands() {
  if (Serial.available() > 0) {
    // If we are in transmit mode, ANY char stops it
    if (transmitMode) {
      // Consume all input
      while (Serial.available())
        Serial.read();

      transmitMode = false;
      irReceiver.enableIRIn(); // Re-enable receiver
      Serial.println(">> STOPPED Transmission. Switched to RECEIVE mode.");
      return;
    }

    // Otherwise, parse command
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() == 0)
      return;

    char cmd = input.charAt(0);

    if (cmd == 't' || cmd == 'T') {
      // Check if there's a code provided: "t 0xC16DB24D"
      int spaceIdx = input.indexOf(' ');
      if (spaceIdx > 0) {
        String codeStr = input.substring(spaceIdx + 1);
        lastReceivedCode = strtoull(codeStr.c_str(), NULL, 16);
        lastProtocol = NEC; // Default to NEC for manual entry
        lastBits = 32;
        Serial.print(">> Manual Code Set: 0x");
        serialPrintUint64(lastReceivedCode, HEX);
        Serial.println();
      }

      startTransmitLoop();
    }
  }
}

void receiveIR() {
  if (irReceiver.decode(&results)) {
    // Check for overflow
    if (results.overflow) {
      Serial.println("WARNING: IR code too long. Buffer full. Potential noise "
                     "or very long code.");
      // We still try to display what we got, just warn
    }

    // Filter out short UNKNOWN codes (Noise)
    if (results.decode_type == UNKNOWN && results.bits < 12) {
      // Ignore likely noise
    } else if (results.value == 0xFFFFFFFFFFFFFFFF && lastProtocol == NEC) {
      Serial.println("--- Repeat Code (held down) ---");
    } else {
      // Store the data
      lastProtocol = results.decode_type;
      lastBits = results.bits;
      lastReceivedCode = results.value;
      lastRawLen = results.rawlen;

      // Generic copy for raw data
      for (uint16_t i = 0; i < results.rawlen && i < 256; i++) {
        lastRawData[i] = results.rawbuf[i];
      }

      Serial.println();
      Serial.println("--- IR Signal Received ---");
      Serial.print("Protocol: ");
      Serial.println(typeToString(results.decode_type));
      Serial.print("Code: 0x");
      serialPrintUint64(results.value, HEX);
      Serial.println();
      Serial.print("Bits: ");
      Serial.println(results.bits);
      Serial.println("-------------------------");
    }

    // Resume listening
    irReceiver.resume();
  }
}

void startTransmitLoop() {
  if (lastReceivedCode == 0) {
    Serial.println(">> ERROR: No code to transmit. Use 't 0x...' to set one.");
    return;
  }

  transmitMode = true;
  Serial.println();
  Serial.println(">>> STARTING TRANSMIT LOOP <<<");
  Serial.println("Sending NEC Code repeatedly...");
  Serial.println("Type ANY character to STOP.");
  Serial.println("------------------------------");
}

// simulateTransmit is deprecated/replaced by startTransmitLoop logic
void simulateTransmit() { startTransmitLoop(); }
