#include <WiFi.h>
#include <esp_now.h>

// Global buffer for serial input
String inputBuffer = "";

// Function to convert MAC address array to String
String macToString(const uint8_t *macAddr) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", macAddr[0],
           macAddr[1], macAddr[2], macAddr[3], macAddr[4], macAddr[5]);
  return String(macStr);
}

// Function to convert String MAC to uint8_t array
void stringToMac(String macStr, uint8_t *macAddr) {
  int values[6];
  if (6 == sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &values[0], &values[1],
                  &values[2], &values[3], &values[4], &values[5])) {
    for (int i = 0; i < 6; ++i) {
      macAddr[i] = (uint8_t)values[i];
    }
  }
}

// Callback when data is received via ESP-NOW (ESP32 Core v3.0 compatible)
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData,
                int len) {
  // Format: "RX:<MAC>:<RAW_DATA>"
  String macStr = macToString(info->src_addr);

  Serial.print("RX:");
  Serial.print(macStr);
  Serial.print(":");

  // Send raw bytes safely if needed, but assuming text/json for now as per
  // prompt.
  for (int i = 0; i < len; i++) {
    Serial.write(incomingData[i]);
  }
  Serial.println(); // End of message
}

// Callback when data is sent
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // Optional: Report delivery status to Pi?
  // Serial.print("STATUS:");
  // Serial.print(macToString(info->dest_addr));
  // Serial.println(status == ESP_NOW_SEND_SUCCESS ? ":SUCCESS" : ":FAIL");
}

void setup() {
  // Init Serial
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  // Init WiFi
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register callbacks
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  Serial.println("Master Gateway Started");
}

void loop() {
  // Listen for data from Pi
  // Format expected: "TX:<TARGET_MAC>:<DATA>\n"
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      processSerialCommand(inputBuffer);
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }
}

void processSerialCommand(String cmd) {
  cmd.trim();
  if (!cmd.startsWith("TX:")) {
    // Only handle TX commands
    return;
  }

  // Parse TX:<MAC>:<DATA>
  // Find first colon after TX
  int firstColon = cmd.indexOf(':'); // Index 2
  int secondColon = cmd.indexOf(':', firstColon + 1);

  if (secondColon == -1) {
    Serial.println("ERR:Invalid Format");
    return;
  }

  String macStr = cmd.substring(firstColon + 1, secondColon);
  String payload = cmd.substring(secondColon + 1);

  // Prepare Target Peer
  uint8_t peerAddr[6];
  stringToMac(macStr, peerAddr);

  // Check if peer exists, if not add it
  if (!esp_now_is_peer_exist(peerAddr)) {
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, peerAddr, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("ERR:Add Peer Failed");
      return;
    }
  }

  // Send Data
  esp_err_t result =
      esp_now_send(peerAddr, (uint8_t *)payload.c_str(), payload.length());

  if (result == ESP_OK) {
    Serial.println("OK:Sent");
  } else {
    Serial.println("ERR:Send Failed");
  }
}
