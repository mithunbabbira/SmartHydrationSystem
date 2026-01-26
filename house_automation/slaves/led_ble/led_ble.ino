#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <WiFi.h>
#include <esp_now.h>

// ==================== Configuration ====================
// Target LED Strip MAC Address
static String targetAddress = "ff:ff:bc:09:a5:b9";

// Triones / HappyLighting Service & Char UUIDs
static BLEUUID serviceUUID("FFD5");
static BLEUUID charUUID("FFD9");

// Protocol Headers
const byte HEADER_COLOR = 0x56;
const byte HEADER_POWER = 0xCC;
const byte HEADER_MODE = 0xBB;

// ==================== ESP-NOW Protocol ====================
// Protocol Commands (Matching Hydration System)
enum CmdType {
  CMD_SET_LED = 0x10, // Power (Used for On/Off)
  CMD_SET_RGB = 0x12  // Color ID
};

// Data Packet (6 Bytes)
typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t command;
  uint32_t data; // Float bits (Standard from Controller)
} ControlPacket;

ControlPacket incomingPacket;
volatile bool packetReceived = false;

// ==================== Globals ====================
BLEClient *pClient;
BLERemoteCharacteristic *pRemoteCharacteristic;
bool connected = false;

// ==================== Callbacks ====================
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData,
                int len) {
  if (len == sizeof(ControlPacket)) {
    memcpy(&incomingPacket, incomingData, sizeof(ControlPacket));
    packetReceived = true;
  }
}

// ==================== BLE Functions ====================
void sendColor(uint8_t r, uint8_t g, uint8_t b) {
  if (!connected || pRemoteCharacteristic == nullptr)
    return;
  uint8_t command[] = {HEADER_COLOR, r, g, b, 0x00, 0xF0, 0xAA};
  pRemoteCharacteristic->writeValue(command, sizeof(command));
  Serial.printf("BLE TX: Color R%d G%d B%d\n", r, g, b);
}

void sendPower(bool on) {
  if (!connected || pRemoteCharacteristic == nullptr)
    return;
  uint8_t stateByte = on ? 0x23 : 0x24;
  uint8_t command[] = {HEADER_POWER, stateByte, 0x33};
  pRemoteCharacteristic->writeValue(command, sizeof(command));
  Serial.printf("BLE TX: Power %s\n", on ? "ON" : "OFF");
}

void sendMode(uint8_t mode, uint8_t speed) {
  if (!connected || pRemoteCharacteristic == nullptr)
    return;
  uint8_t command[] = {HEADER_MODE, mode, speed, 0x44};
  pRemoteCharacteristic->writeValue(command, sizeof(command));
  Serial.printf("BLE TX: Mode %d Speed %d\n", mode, speed);
}

// ==================== Connection Logic ====================
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) { Serial.println(">>> BLE Connected!"); }
  void onDisconnect(BLEClient *pclient) {
    connected = false;
    Serial.println(">>> BLE Disconnected!");
  }
};

bool connectToDevice() {
  Serial.print("Connecting to BLE: ");
  Serial.println(targetAddress);
  BLEAddress addr(targetAddress.c_str());

  if (!pClient->connect(addr)) {
    Serial.println("Connect Failed");
    return false;
  }

  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    pClient->disconnect();
    return false;
  }

  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    pClient->disconnect();
    return false;
  }

  connected = true;
  Serial.println("BLE Ready.");
  return true;
}

// ==================== Logic ====================
void processIncomingPackets() {
  if (packetReceived) {
    packetReceived = false;
    Serial.printf("RX CMD: 0x%02X Data: 0x%08X\n", incomingPacket.command,
                  incomingPacket.data);

    // Interpret Data
    // Controller sends everything as float-packed-in-uint32
    // But boolean 1 might just be integer 1 depending on sender version
    // We check both raw and float conversion

    float f;
    memcpy(&f, &incomingPacket.data, 4);
    int val = (int)f; // Cast float to int (e.g. 1.0 -> 1)

    // Fallback: if data is small integer (not a float representation), use
    // directly
    if (incomingPacket.data < 1000 && incomingPacket.data > 0)
      val = incomingPacket.data;

    switch (incomingPacket.command) {
    case CMD_SET_LED:
      // Set Power
      sendPower(val > 0 || incomingPacket.data > 0);
      break;

    case CMD_SET_RGB:
      // Set Color ID
      Serial.printf("Set Color ID: %d\n", val);
      switch (val) {
      case 0:
        sendPower(false);
        break; // OFF
      case 1:
        sendColor(255, 0, 0);
        break; // Red
      case 2:
        sendColor(0, 255, 0);
        break; // Green
      case 3:
        sendColor(0, 0, 255);
        break; // Blue
      case 4:
        sendColor(255, 255, 255);
        break; // White
      case 6:
        sendColor(255, 165, 0);
        break; // Orange
      case 8:
        sendColor(200, 0, 255);
        break; // Purple
      default:
        Serial.println("Unknown Color ID");
        break;
      }
      if (val > 0)
        sendPower(true); // Ensure ON
      break;
    }
  }
}

// ==================== Main ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\nBooting LED BLE + ESP-NOW Proxy...");

  // 1. Init WiFi (STA Mode required for ESP-NOW)
  WiFi.mode(WIFI_STA);
  delay(500); // Wait for MAC to be available
  Serial.print("MY MAC ADDRESS: ");
  Serial.println(WiFi.macAddress());

  // 2. Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  // 3. Init BLE
  BLEDevice::init("ESP32_LED_Proxy");
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  Serial.println("System Ready. Waiting for Commands or Connection...");
}

void loop() {
  // Maintain BLE
  static unsigned long lastBleCheck = 0;
  if (!connected) {
    if (millis() - lastBleCheck > 5000) {
      lastBleCheck = millis();
      connectToDevice();
    }
  }

  // Handle Commands
  processIncomingPackets();

  delay(10);
}
