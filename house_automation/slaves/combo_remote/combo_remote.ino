#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <WiFi.h>
#include <esp_now.h>

// ==================== Configuration ====================
// --- IR ---
const uint16_t kIrLed = 13; // GPIO 13
IRsend irsend(kIrLed);

// --- BLE ---
// Target LED Strip MAC Address
static String targetAddress = "ff:ff:bc:09:a5:b9";
// Service & Char UUIDs (Triones)
static BLEUUID serviceUUID("FFD5");
static BLEUUID charUUID("FFD9");

// Protocol Headers
const byte HEADER_COLOR = 0x56;
const byte HEADER_POWER = 0xCC;
const byte HEADER_MODE = 0xBB;

// ==================== Protocol ====================
enum DeviceType { TYPE_HYDRATION = 1, TYPE_LED = 2, TYPE_IR = 3 };

enum CmdType {
  CMD_SET_LED = 0x10,  // Power
  CMD_SET_RGB = 0x12,  // Color ID
  CMD_SET_MODE = 0x13, // Mode/Speed
  CMD_SEND_NEC = 0x31  // IR Send
};

// Data Packet (6 Bytes)
typedef struct __attribute__((packed)) {
  uint8_t type;
  uint8_t command;
  uint32_t data;
} ControlPacket;

ControlPacket incomingPacket;
volatile bool packetReceived = false;

// Raw BLE Buffer
volatile int rawBleLen = 0;
uint8_t rawBleBuffer[20];

// ==================== Globals ====================
// BLE State
BLEClient *pClient;
BLERemoteCharacteristic *pRemoteCharacteristic;
bool connected = false;

// ==================== BLE Helper Functions ====================
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

void sendBleData(uint8_t *data, size_t len) {
  if (!connected || pRemoteCharacteristic == nullptr)
    return;
  pRemoteCharacteristic->writeValue(data, len);
}

void sendColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t command[] = {HEADER_COLOR, r, g, b, 0x00, 0xF0, 0xAA};
  sendBleData(command, sizeof(command));
  Serial.printf("BLE TX: Color R%d G%d B%d\n", r, g, b);
}

void sendPower(bool on) {
  uint8_t stateByte = on ? 0x23 : 0x24;
  uint8_t command[] = {HEADER_POWER, stateByte, 0x33};
  sendBleData(command, sizeof(command));
}

void sendMode(uint8_t mode, uint8_t speed) {
  uint8_t command[] = {HEADER_MODE, mode, speed, 0x44};
  sendBleData(command, sizeof(command));
}

// ==================== Logic Handlers ====================

void handleLedCommand(uint8_t cmd, uint32_t data) {
  // Controller sends floats often, or pure ints.
  // We check both.
  float f;
  memcpy(&f, &data, 4);
  int val = (int)f;
  // Fallback if data looks like integer
  if (data < 1000 && data > 0)
    val = data;

  switch (cmd) {
  case CMD_SET_LED:
    sendPower(val > 0 || data > 0);
    Serial.printf("LED Power: %d\n", val > 0);
    break;
  case CMD_SET_RGB:
    Serial.printf("LED Color ID: %d\n", val);
    switch (val) {
    case 0:
      sendPower(false);
      break;
    case 1:
      sendColor(255, 0, 0);
      break;
    case 2:
      sendColor(0, 255, 0);
      break;
    case 3:
      sendColor(0, 0, 255);
      break;
    case 4:
      sendColor(255, 255, 255);
      break;
    case 6:
      sendColor(255, 165, 0);
      break; // Orange
    case 8:
      sendColor(200, 0, 255);
      break; // Purple
    case 37:
      sendMode(37, 100);
      break; // Rainbow shortcut
    default:
      break;
    }
    if (val > 0)
      sendPower(true);
    break;
  case CMD_SET_MODE: {
    uint8_t speed = data & 0xFF;
    uint8_t mode = (data >> 8) & 0xFF;
    sendMode(mode, speed);
    sendPower(true);
  } break;
  }
}

void handleIrCommand(uint8_t cmd, uint32_t data) {
  if (cmd == CMD_SEND_NEC) {
    Serial.printf("IR TX NEC: 0x%08X\n", data);
    irsend.sendNEC(data, 32);
  }
}

// ==================== ESP-NOW Callback ====================
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData,
                int len) {
  if (len == sizeof(ControlPacket)) {
    ControlPacket *packet = (ControlPacket *)incomingData;

    Serial.printf("RX Type:%d Cmd:0x%02X Data:0x%08X\n", packet->type,
                  packet->command, packet->data);

    // Dispatch
    if (packet->type == TYPE_LED) {
      handleLedCommand(packet->command, packet->data);
    } else if (packet->type == TYPE_IR) {
      handleIrCommand(packet->command, packet->data);
    } else {
      Serial.println("Unknown Type");
    }
  } else if (len > 0 && len <= 20) {
    // Raw BLE Passthrough
    if (connected && pRemoteCharacteristic != nullptr) {
      pRemoteCharacteristic->writeValue((uint8_t *)incomingData, len);
      Serial.printf("BLE TX Raw: %d bytes\n", len);
    }
  }
}

// ==================== Main ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- Combined Remote (ESP32-CAM) ---");

  // 1. WiFi & MAC
  WiFi.mode(WIFI_STA);
  delay(500);
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
  WiFi.setSleep(false);

  // 2. IR
  irsend.begin();
  Serial.printf("IR Init on Pin %d\n", kIrLed);

  // 3. ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);

  // 4. BLE
  BLEDevice::init("ESP32_Combo_Remote");
  pClient = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyClientCallback());

  // Boot Sequence
  delay(2000);
  irsend.sendNEC(0xF7F00F, 32);
  Serial.println("Boot Sequence: Sent 0xF7F00F");

  Serial.println("Ready.");
}

void loop() {
  // BLE Auto-reconnect
  static unsigned long lastBleCheck = 0;
  if (!connected) {
    if (millis() - lastBleCheck > 5000) {
      lastBleCheck = millis();
      connectToDevice();
    }
  }
  delay(50);
}
