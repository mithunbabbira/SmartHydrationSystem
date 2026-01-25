#include <WiFi.h>
#include <esp_now.h>

// MASTER MAC ADDRESS (Given by User)
uint8_t masterMAC[] = {0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4};

// Global buffer for serial input
String inputBuffer = "";

// Helper to print MAC
void printMAC(const uint8_t *mac_addr) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0],
           mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
}

// Callback when data is sent (Core v3 Signature)
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Delivery Success");
  } else {
    Serial.println("Delivery Fail");
  }
}

// Callback when data is received (Core v3 Signature)
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData,
                int len) {
  Serial.print("\n--- Data Received from ");
  printMAC(info->src_addr);
  Serial.println(" ---");
  Serial.print("Content: ");
  for (int i = 0; i < len; i++) {
    Serial.print((char)incomingData[i]);
  }
  Serial.println("\n----------------------------\n");
}

void setup() {
  // Init Serial
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  } // Wait for serial

  // Init WiFi
  WiFi.mode(WIFI_STA);
  Serial.println("Slave Node Started");
  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Register Peer (Master)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterMAC, 6);
  peerInfo.channel = 0; // 0 = Use current channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("Master Peer Registered.");
  Serial.println("Type a message and press Enter to send to Pi/Master.");
}

void loop() {
  // Read Serial to send data to Master
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      sendDataToMaster(inputBuffer);
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }
}

void sendDataToMaster(String data) {
  if (data.length() == 0)
    return;

  data.trim(); // Remove whitespace/newlines (user input convenience)

  Serial.print("Sending to Master: ");
  Serial.println(data);

  esp_err_t result =
      esp_now_send(masterMAC, (uint8_t *)data.c_str(), data.length());

  if (result != ESP_OK) {
    Serial.println("Error sending the data");
  }
}
