/*
 * ESP-NOW Master (Arduino) - Sensor Data Receiver
 * Hardware: ESP32
 * Receives sensor data from multiple slaves
 *
 * Arduino IDE Setup:
 * - Board: ESP32 Dev Module (or your ESP32 board)
 * - Upload Speed: 115200
 * - Flash Frequency: 80MHz
 */

#include <WiFi.h>
#include <esp_now.h>

// Configuration
#define MAX_SLAVES 5

// Slave info structure
typedef struct {
  uint8_t mac[6];
  int8_t rssi;
  uint32_t messages_received;
  bool active;
  char name[16];
  uint8_t slave_id;
  float last_sensor_value;
} SlaveInfo;

// Slave sensor data structure
typedef struct {
  uint8_t slave_id;   // Unique ID for this slave
  uint32_t counter;   // Message counter
  uint32_t timestamp; // Message timestamp
  float sensor_value; // Simulated sensor data
  int8_t rssi;        // RSSI (if available)
  uint32_t free_heap; // Free heap memory
} SensorData;

// Global variables
SlaveInfo slaves[MAX_SLAVES];
uint32_t total_messages_received = 0;

// Find slave by MAC
int findSlaveByMAC(const uint8_t *mac) {
  for (int i = 0; i < MAX_SLAVES; i++) {
    if (slaves[i].active && memcmp(slaves[i].mac, mac, 6) == 0) {
      return i;
    }
  }
  return -1;
}

// Find free slot
int findFreeSlot() {
  for (int i = 0; i < MAX_SLAVES; i++) {
    if (!slaves[i].active) {
      return i;
    }
  }
  return -1;
}

// Register new slave
int registerSlave(const uint8_t *mac, uint8_t slave_id) {
  int existing = findSlaveByMAC(mac);
  if (existing >= 0)
    return existing;

  int slot = findFreeSlot();
  if (slot < 0) {
    Serial.println("❌ No free slots!");
    return -1;
  }

  memcpy(slaves[slot].mac, mac, 6);
  slaves[slot].active = true;
  slaves[slot].messages_received = 0;
  slaves[slot].slave_id = slave_id;
  snprintf(slaves[slot].name, 16, "Slave-%02X", slave_id);

  Serial.printf(
      "✓ Slave [%d] registered: %02X:%02X:%02X:%02X:%02X:%02X (ID: %d, %s)\n",
      slot, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], slave_id,
      slaves[slot].name);

  return slot;
}

// Callback when data is received
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data,
                int len) {
  if (len != sizeof(SensorData))
    return;

  SensorData sensor;
  memcpy(&sensor, data, sizeof(SensorData));

  // Extract MAC address and RSSI from recv_info
  const uint8_t *mac = recv_info->src_addr;
  int8_t actual_rssi = recv_info->rx_ctrl->rssi; // Real RSSI from packet

  // Find or register slave
  int idx = findSlaveByMAC(mac);
  if (idx < 0) {
    idx = registerSlave(mac, sensor.slave_id);
    if (idx < 0)
      return; // No free slots
  }

  // Update slave stats
  slaves[idx].messages_received++;
  slaves[idx].rssi =
      actual_rssi; // Use actual RSSI from packet, not slave-provided
  slaves[idx].last_sensor_value = sensor.sensor_value;
  total_messages_received++;

  // Calculate message rate
  uint32_t uptime_sec = millis() / 1000;
  float msg_per_sec =
      uptime_sec > 0 ? (slaves[idx].messages_received * 1.0 / uptime_sec) : 0;

  Serial.printf("📩 DATA [%d] %s | Counter: %lu | Sensor: %.1f | "
                "RSSI: %d dBm | Rate: %.2f msg/s | Heap: %lu KB\n",
                idx, slaves[idx].name, sensor.counter, sensor.sensor_value,
                actual_rssi, msg_per_sec, sensor.free_heap / 1024);
}

// Callback when data is sent (ESP32 Arduino Core v3.x compatible)
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  // Optional: track send status
}

// Print statistics
void printStats() {
  int active_count = 0;

  Serial.println("═══════════════════════════════════════════════════════");
  Serial.println("MASTER STATISTICS:");

  for (int i = 0; i < MAX_SLAVES; i++) {
    if (!slaves[i].active)
      continue;
    active_count++;

    uint32_t uptime_sec = millis() / 1000;
    float msg_per_sec =
        uptime_sec > 0 ? (slaves[i].messages_received * 1.0 / uptime_sec) : 0;

    Serial.printf("  [%d] %s | ID: %d | RSSI: %d dBm | Last Sensor: %.1f | "
                  "Messages: %lu | Rate: %.2f msg/s\n",
                  i, slaves[i].name, slaves[i].slave_id, slaves[i].rssi,
                  slaves[i].last_sensor_value, slaves[i].messages_received,
                  msg_per_sec);
  }

  Serial.printf("  Active Slaves: %d/%d | Total Messages: %lu\n", active_count,
                MAX_SLAVES, total_messages_received);
  Serial.printf("  Heap: %lu KB | Uptime: %lu s\n", ESP.getFreeHeap() / 1024,
                millis() / 1000);
  Serial.println("═══════════════════════════════════════════════════════");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("╔════════════════════════════════════════════════════════╗");
  Serial.println("║       ESP-NOW MASTER (Arduino - Data Receiver)        ║");
  Serial.println("║  Max Slaves: 5 | Continuous reception monitoring      ║");
  Serial.println("╚════════════════════════════════════════════════════════╝");

  // Initialize WiFi in STA mode
  WiFi.mode(WIFI_STA);
  delay(500); // Allow WiFi hardware to initialize

  Serial.print("Master MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init failed");
    return;
  }

  Serial.println("✓ ESP-NOW initialized");

  // Register callbacks
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  // Initialize slave array
  memset(slaves, 0, sizeof(slaves));

  Serial.println("✓ Master ready - waiting for slave data...");
  Serial.println();
}

void loop() {
  // Print statistics every 10 seconds
  static unsigned long last_stats = 0;
  if (millis() - last_stats >= 10000) {
    last_stats = millis();
    printStats();
  }

  delay(10);
}
