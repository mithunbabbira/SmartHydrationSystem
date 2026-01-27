/*
 * ESP8266 IR Remote Node (ESP-NOW Slave)
 * Hardware: NodeMCU ESP8266
 * IR LED: GPIO 4 (D2)
 * Status LED: GPIO 2 (D4, Builtin)
 */

#include <ESP8266WiFi.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <espnow.h>

// ==================== Configuration ====================
// Master MAC (Must match your Master ESP32)
uint8_t masterMac[] = {0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4};

// Pins
const uint16_t IR_SEND_PIN = 4; // D2

// Objects
IRsend irSender(IR_SEND_PIN);

// Protocol Commands
enum CmdType { CMD_SEND_NEC = 0x31 };

// Data Packet (6 Bytes - Matching Main System)
typedef struct __attribute__((packed)) {
  uint8_t type; // 3 = IR (We can define this arbitrarily for Slaves)
  uint8_t command;
  uint32_t data;
} ControlPacket;

ControlPacket incomingPacket;

// ==================== Callbacks ====================
// ESP8266 Callback Signature: (u8 *mac, u8 *data, u8 len)
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  // Verify sender is Master? (Optional, but good practice)
  // For now, accept from anyone to verify connectivity easily

  if (len == sizeof(ControlPacket)) {
    memcpy(&incomingPacket, incomingData, sizeof(ControlPacket));

    // Interpret Data
    if (incomingPacket.command == CMD_SEND_NEC) {
      uint32_t code = incomingPacket.data;
      Serial.printf("IR TX NEC: 0x%08X\n", code);

      // Blink
      digitalWrite(LED_BUILTIN, LOW);
      irSender.sendNEC(code, 32);
      digitalWrite(LED_BUILTIN, HIGH);
    }
  } else {
    Serial.printf("Unknown Packet Len: %d\n", len);
  }
}

void OnDataSent(uint8_t *mac_addr, uint8_t status) {
  // Only essential if we send data back
}

// ==================== Main ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\nBooting IR Remote Node (ESP8266)...");

  // Init IR
  irSender.begin();

  // Init LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // OFF

  // Init WiFi
  WiFi.mode(WIFI_STA);
  delay(100);
  Serial.print("MY MAC ADDRESS: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Set Role (ESP8266 Specific)
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);

  // Register Peers
  esp_now_add_peer(masterMac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

  // Register Callbacks
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent); // Optional

  Serial.println("System Ready. Waiting for IR Commands...");
}

void loop() {
  // Nothing to do in loop, everything is event-driven
  yield();
}
