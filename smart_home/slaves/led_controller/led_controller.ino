/*
 * Smart Home Slave ID 2: BLE LED Controller
 * -----------------------------------------
 * Features: Controls a BLE LED strip based on ESP-NOW commands.
 *
 * Hardware: ESP32
 */

#include "../../master_gateway/protocol.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <WiFi.h>
#include <esp_now.h>

// --- BLE Config ---
static BLEUUID serviceUUID("FFD5");
static BLEUUID charUUID("FFD9");
String targetAddress = "ff:ff:bc:09:a5:b9";
BLERemoteCharacteristic *pRemoteCharacteristic;
BLEClient *pClient = nullptr;
bool connected = false;

// --- State ---
LEDData current_state;
unsigned long last_send_time = 0;

// --- BLE Functions ---
bool connectToBLE() {
  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
  }

  if (pClient->isConnected())
    return true;

  Serial.printf("Attempting BLE connection to %s...\n", targetAddress.c_str());
  if (pClient->connect(BLEAddress(targetAddress.c_str()))) {
    BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService != nullptr) {
      pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
      if (pRemoteCharacteristic != nullptr) {
        connected = true;
        Serial.println("✓ BLE Connected to Strip");
        return true;
      }
    }
  }
  Serial.println("✗ BLE Connection Failed");
  return false;
}

void sendColorToStrip(uint8_t r, uint8_t g, uint8_t b) {
  if (!connected)
    return;
  uint8_t packet[] = {0x56, r, g, b, 0x00, 0xF0, 0xAA};
  pRemoteCharacteristic->writeValue(packet, sizeof(packet));
}

void sendPowerToStrip(bool on) {
  if (!connected)
    return;
  uint8_t state = on ? 0x23 : 0x24;
  uint8_t packet[] = {0xCC, state, 0x33};
  pRemoteCharacteristic->writeValue(packet, sizeof(packet));
}

void sendModeToStrip(uint8_t mode, uint8_t speed) {
  if (!connected)
    return;
  uint8_t packet[] = {0xBB, mode, speed, 0x44};
  pRemoteCharacteristic->writeValue(packet, sizeof(packet));
}

// --- ESP-NOW Callbacks ---

void onDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data,
                int len) {
  if (len < sizeof(ESPNowHeader))
    return;
  ESPNowHeader *header = (ESPNowHeader *)data;

  if (header->slave_id == 0 && header->msg_type == MSG_TYPE_COMMAND) {
    LEDData *ld = (LEDData *)data;
    current_state = *ld;

    if (connected) {
      if (!ld->is_on) {
        sendPowerToStrip(false);
      } else {
        sendPowerToStrip(true);
        delay(10);
        if (ld->mode > 0) {
          sendModeToStrip(ld->mode, ld->speed);
        } else {
          sendColorToStrip(ld->r, ld->g, ld->b);
        }
      }
    }
  }
}

void sendHeartbeat() {
  ESPNowHeader header;
  header.slave_id = SLAVE_ID_LED;
  header.msg_type = MSG_TYPE_TELEMETRY;
  header.version = PROTOCOL_VERSION;

  uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcast_mac, (uint8_t *)&header, sizeof(header));
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.begin(115200);

  // BLE Init
  BLEDevice::init("SmartHome-LED-Bridge");

  // WiFi & ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK)
    return;

  esp_now_register_recv_cb(onDataRecv);

  // Add broadcast peer (Master)
  esp_now_peer_info_t peerInfo = {};
  memset(peerInfo.peer_addr, 0xFF, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("LED Slave Ready");
}

void loop() {
  // Warn if user accidentally connects Slave to Pi
  if (Serial.available()) {
    while (Serial.available())
      Serial.read();
    Serial.println("{\"warning\":\"Slave connected to Serial. Please connect "
                   "Master gateway to Pi instead.\"}");
  }

  if (!connected) {
    static unsigned long last_connect_attempt = 0;
    if (millis() - last_connect_attempt > 30000) { // Retry every 30s
      last_connect_attempt = millis();
      connectToBLE();
    }
  }

  // Heartbeat every 5s for discovery
  static unsigned long last_heartbeat = 0;
  if (millis() - last_heartbeat > 5000) {
    last_heartbeat = millis();
    sendHeartbeat();
  }
  delay(100);
}
