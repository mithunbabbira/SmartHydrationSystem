#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <WiFi.h>
#include <esp_now.h>

// ESP32-CAM GPIO 13 is generally free and accessible
const uint16_t kIrLed = 13;
IRsend irsend(kIrLed);

// Data structure (Must match sender)
typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t command;
  uint32_t data;
} ControlPacket;

// Commands
#define CMD_SEND_NEC 0x31

// Callback when data is received
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData,
                int len) {
  Serial.print("Bytes received: ");
  Serial.println(len);

  if (len == sizeof(ControlPacket)) {
    ControlPacket *packet = (ControlPacket *)incomingData;

    Serial.printf("Type: %d, Cmd: 0x%02X, Data: 0x%08X\n", packet->type,
                  packet->command, packet->data);

    // Check for IR Command (Type 3 in our plan, or just process command)
    if (packet->command == CMD_SEND_NEC) {
      Serial.printf("Sending NEC IR Code: 0x%08X\n", packet->data);
      // sendNEC(data, nbits)
      irsend.sendNEC(packet->data, 32);
      Serial.println("Sent!");
    }
  } else {
    Serial.println("Invalid packet size for ControlPacket");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Give time for Serial to stabilize

  WiFi.mode(WIFI_STA);
  delay(500); // Wait for MAC to be available
  // Disable power saving for lower latency
  WiFi.setSleep(false);

  Serial.println("--- ESP32-CAM IR Device ---");
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register callback
  esp_now_register_recv_cb(OnDataRecv);

  irsend.begin();
  Serial.printf("IR Sender started on Pin %d\n", kIrLed);
}

void loop() {
  // No main loop logic needed, everything is interrupt/callback driven
  delay(2000);
}
