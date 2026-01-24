/*
 * ESP-NOW Slave (Arduino) - Standalone Mode
 * Hardware: ESP32 or ESP32-CAM
 * Continuously sends sensor data to hardcoded master MAC
 *
 * Arduino IDE Setup:
 * - Board: ESP32 Dev Module (or ESP32 Wrover Module for ESP32-CAM)
 * - Upload Speed: 115200
 * - Flash Frequency: 80MHz
 *
 * For ESP32-CAM:
 * - Board: AI Thinker ESP32-CAM
 * - Use USB-to-Serial adapter for programming
 */

#include <WiFi.h>
#include <esp_now.h>

// MASTER MAC ADDRESS - Update this with your master's MAC
uint8_t masterMAC[] = {0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4};

// Message structure
typedef struct {
  uint8_t slave_id;   // Unique ID for this slave
  uint32_t counter;   // Message counter
  uint32_t timestamp; // Message timestamp
  float sensor_value; // Simulated sensor data
  int8_t rssi;        // RSSI (if available)
  uint32_t free_heap; // Free heap memory
} SensorData;

// Global variables
uint32_t message_counter = 0;
uint32_t messages_sent = 0;
uint32_t send_failures = 0;

// Unique slave ID (based on last 2 bytes of MAC)
uint8_t slave_id = 0;

// Callback when data is sent (ESP32 Arduino Core v3.x compatible)
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    messages_sent++;
    Serial.printf("✓ Message #%lu sent successfully\n", message_counter);
  } else {
    send_failures++;
    Serial.printf("❌ Message #%lu failed to send\n", message_counter);
  }
}

// Send sensor data to master
void sendSensorData() {
  SensorData data;

  data.slave_id = slave_id;
  data.counter = message_counter++;
  data.timestamp = millis();
  data.sensor_value = random(200, 300) / 10.0; // Simulated sensor (20.0-30.0)
  data.rssi = WiFi.RSSI();
  data.free_heap = ESP.getFreeHeap();

  esp_err_t result = esp_now_send(masterMAC, (uint8_t *)&data, sizeof(data));

  if (result == ESP_OK) {
    Serial.printf("📤 Sent: ID=%d | Counter=%lu | Sensor=%.1f | Heap=%lu KB\n",
                  data.slave_id, data.counter, data.sensor_value,
                  data.free_heap / 1024);
  } else {
    Serial.printf("❌ esp_now_send failed: %s\n", esp_err_to_name(result));
  }
}

// Print statistics
void printStats() {
  float success_rate =
      message_counter > 0 ? (messages_sent * 100.0 / message_counter) : 0;

  Serial.println("═══════════════════════════════════════════════════════");
  Serial.println("SLAVE STATISTICS:");
  Serial.printf("  Slave ID: %d | Uptime: %lu s\n", slave_id, millis() / 1000);
  Serial.printf("  Messages: %lu | Sent: %lu | Failed: %lu\n", message_counter,
                messages_sent, send_failures);
  Serial.printf("  Success Rate: %.1f%% | Heap: %lu KB\n", success_rate,
                ESP.getFreeHeap() / 1024);
  Serial.println("═══════════════════════════════════════════════════════");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("╔════════════════════════════════════════════════════════╗");
  Serial.println("║   ESP-NOW SLAVE (Standalone - Arduino ESP32/CAM)     ║");
  Serial.println("║   Continuous sensor data transmission to master       ║");
  Serial.println("╚════════════════════════════════════════════════════════╝");

  // Initialize WiFi in STA mode
  WiFi.mode(WIFI_STA);
  delay(500); // Allow WiFi hardware to initialize

  Serial.print("Slave MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Get slave ID from last byte of MAC
  uint8_t mac[6];
  WiFi.macAddress(mac);
  slave_id = mac[5]; // Use last byte as unique ID

  Serial.printf("Slave ID: %d (0x%02X)\n", slave_id, slave_id);
  Serial.printf("Master MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", masterMAC[0],
                masterMAC[1], masterMAC[2], masterMAC[3], masterMAC[4],
                masterMAC[5]);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }

  Serial.println("✓ ESP-NOW initialized");

  // Register send callback
  esp_now_register_send_cb(OnDataSent);

  // Add master as peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterMAC, 6);
  peerInfo.channel = 0; // Use current channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    Serial.println("✓ Master peer added");
  } else {
    Serial.println("❌ Failed to add master peer");
    return;
  }

  Serial.println("✓ Slave ready - starting transmission...");
  Serial.println();
}

void loop() {
  // Send sensor data every 1 second
  static unsigned long last_send = 0;
  if (millis() - last_send >= 1) {
    last_send = millis();
    sendSensorData();
  }

  // Print statistics every 10 seconds
  static unsigned long last_stats = 0;
  if (millis() - last_stats >= 10000) {
    last_stats = millis();
    printStats();
  }

  delay(10);
}
