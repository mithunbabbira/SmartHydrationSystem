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
#include <esp_wifi.h>

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

// Command queue (to avoid BLE calls in ESP-NOW callback)
bool pending_command = false;
LEDData pending_state;

// --- BLE Callbacks ---
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {
    connected = true;
    Serial.println(">>> BLE Connected to Strip");
  }

  void onDisconnect(BLEClient *pclient) {
    connected = false;
    Serial.println(">>> BLE Disconnected from Strip");
  }
};

// --- BLE Functions ---
bool connectToBLE() {
  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
  }

  if (pClient->isConnected())
    return true;

  Serial.printf("Attempting BLE connection to %s...\n", targetAddress.c_str());

  BLEAddress addr(targetAddress.c_str());
  if (!pClient->connect(addr)) {
    Serial.println("✗ BLE Connection Failed");
    return false;
  }

  // Obtain reference to the service
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.println("✗ Failed to find service FFD5");
    pClient->disconnect();
    return false;
  }

  // Obtain reference to the characteristic
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.println("✗ Failed to find characteristic FFD9");
    pClient->disconnect();
    return false;
  }

  Serial.println("✓ BLE Connected to Strip");
  return true;
}

void sendColorToStrip(uint8_t r, uint8_t g, uint8_t b) {
  Serial.printf("[BLE] sendColor: R%d G%d B%d\n", r, g, b);
  if (!connected || pRemoteCharacteristic == nullptr) {
    Serial.println("[BLE] sendColor SKIP - not connected");
    return;
  }
  Serial.println("[BLE] sendColor: Writing packet...");
  uint8_t packet[] = {0x56, r, g, b, 0x00, 0xF0, 0xAA};
  pRemoteCharacteristic->writeValue(packet, sizeof(packet));
  Serial.println("[BLE] sendColor: Done");
}

void sendPowerToStrip(bool on) {
  Serial.printf("[BLE] sendPower: %s\n", on ? "ON" : "OFF");
  if (!connected || pRemoteCharacteristic == nullptr) {
    Serial.println("[BLE] sendPower SKIP - not connected");
    return;
  }
  Serial.println("[BLE] sendPower: Writing packet...");
  uint8_t state = on ? 0x23 : 0x24;
  uint8_t packet[] = {0xCC, state, 0x33};
  pRemoteCharacteristic->writeValue(packet, sizeof(packet));
  Serial.println("[BLE] sendPower: Done");
}

void sendModeToStrip(uint8_t mode, uint8_t speed) {
  Serial.printf("[BLE] sendMode: M%d S%d\n", mode, speed);
  if (!connected || pRemoteCharacteristic == nullptr) {
    Serial.println("[BLE] sendMode SKIP - not connected");
    return;
  }
  Serial.println("[BLE] sendMode: Writing packet...");
  uint8_t packet[] = {0xBB, mode, speed, 0x44};
  pRemoteCharacteristic->writeValue(packet, sizeof(packet));
  Serial.println("[BLE] sendMode: Done");
}

// --- Security ---
// REPLACE THIS WITH YOUR MASTER GATEWAY MAC ADDRESS
// Find it in the serial monitor log of the Master Gateway
uint8_t master_mac[6] = {0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4}; // Gateway MAC
bool master_known = true; // Production mode enabled

// --- ESP-NOW Callbacks ---

void onDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data,
                int len) {
  if (len < sizeof(ESPNowHeader))
    return;
  ESPNowHeader *header = (ESPNowHeader *)data;

  // Security Check: Only accept commands from known Master
  if (master_known) {
    if (memcmp(recv_info->src_addr, master_mac, 6) != 0) {
      return;
    }
  } else {
    // Auto-Learn Master on first command (Dev Mode)
    if (header->slave_id == 0) {
      memcpy(master_mac, recv_info->src_addr, 6);
      master_known = true;
      Serial.println("✓ Master Learned via ESP-NOW");

      // Add Master as Peer for Unicast
      if (!esp_now_is_peer_exist(master_mac)) {
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, master_mac, 6);
        peerInfo.channel = 1;
        peerInfo.encrypt = false;
        esp_now_add_peer(&peerInfo);
      }
    }
  }

  if (header->slave_id == 0 && header->msg_type == MSG_TYPE_COMMAND) {
    LEDData *ld = (LEDData *)data;
    // Store command for processing in loop() - CRITICAL: Don't do BLE in
    // callback!
    pending_state = *ld;
    pending_command = true;
    Serial.println("✓ ESP-NOW Command Received from Master");
  }
}

void sendHeartbeat() {
  ESPNowHeader header;
  header.slave_id = SLAVE_ID_LED;
  header.msg_type = MSG_TYPE_TELEMETRY;
  header.version = PROTOCOL_VERSION;

  // Unicast to Master if known, otherwise Broadcast
  uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t *dest_mac = master_known ? master_mac : broadcast_mac;
  esp_now_send(dest_mac, (uint8_t *)&header, sizeof(header));
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.begin(115200);

  // BLE Init
  BLEDevice::init("SmartHome-LED-Bridge");

  // WiFi & ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // CRITICAL: Fixes WiFi+BLE Coexistence instability
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK)
    return;

  esp_now_register_recv_cb(onDataRecv);

  // Register Master Peer if hardcoded
  esp_now_peer_info_t peerInfo = {};
  if (master_known) {
    memcpy(peerInfo.peer_addr, master_mac, 6);
  } else {
    memset(peerInfo.peer_addr, 0xFF, 6); // Broadcast peer
  }
  peerInfo.channel = 1;
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

  // Process pending ESP-NOW commands (deferred from callback)
  if (pending_command) {
    Serial.println("[PROCESS] Processing pending command...");
    pending_command = false;
    current_state = pending_state;

    Serial.printf("[PROCESS] State: on=%d mode=%d r=%d g=%d b=%d\n",
                  current_state.is_on, current_state.mode, current_state.r,
                  current_state.g, current_state.b);

    if (connected && pClient != nullptr && pClient->isConnected()) {
      Serial.println("[PROCESS] BLE connected, sending commands...");

      if (!current_state.is_on) {
        sendPowerToStrip(false);
        delay(100); // Give controller time to process
      } else {
        sendPowerToStrip(true);
        delay(100);
        if (current_state.mode > 0) {
          sendModeToStrip(current_state.mode, current_state.speed);
        } else {
          sendColorToStrip(current_state.r, current_state.g, current_state.b);
        }
      }
      Serial.println("✓ BLE Command Sent to Strip");
      Serial.printf("[HEAP] Free heap: %d bytes\n", ESP.getFreeHeap());
    } else {
      Serial.println("✗ No BLE Connection - Command Ignored");
      connected = false;
    }
    Serial.println("[PROCESS] Command processing complete");
  }

  // BLE connection retry
  if (!connected) {
    static unsigned long last_connect_attempt = 0;
    if (millis() - last_connect_attempt > 10000) {
      last_connect_attempt = millis();
      connectToBLE();
    }
  }

  // Heartbeat every 10s
  static unsigned long last_heartbeat = 0;
  if (millis() - last_heartbeat > 10000) {
    last_heartbeat = millis();
    sendHeartbeat();
    Serial.println("→ Heartbeat Sent to Master");
  }

  delay(100);
}
