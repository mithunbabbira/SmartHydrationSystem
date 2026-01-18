/*
 * Samsung S25 Ultra Bluetooth Classic Presence Tester
 * ===================================================
 *
 * This code uses the ESP-IDF Bluetooth Classic GAP API to detect a device.
 * It manually triggers a "Remote Name Request" (paging) to a specific MAC.
 * This is the direct equivalent of the 'hcitool' command on Linux.
 */

#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"

// ==================== CONFIGURATION ====================
// Target MAC address (Samsung S25 Ultra)
esp_bd_addr_t target_addr = {0x48, 0xEF, 0x1C, 0x49, 0x6A, 0xE7};

volatile bool found_device = false;
volatile bool scan_finished = false;

// GAP Callback to handle the Name Request result
void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  if (event == ESP_BT_GAP_READ_REMOTE_NAME_EVT) {
    if (param->read_rmt_name.stat == ESP_BT_STATUS_SUCCESS) {
      Serial.printf("[FOUND] Device responded! Name: %s\n",
                    param->read_rmt_name.rmt_name);
      found_device = true;
    } else {
      Serial.println("[SCAN] No response from device.");
    }
    scan_finished = true;
  }
}

bool checkPresence() {
  found_device = false;
  scan_finished = false;

  Serial.print("[SCAN] Pinging Samsung S25 Ultra (48:EF:1C:49:6A:E7)...");

  // Start the Remote Name Request (timeout is handled by stack)
  esp_err_t ret = esp_bt_gap_read_remote_name(target_addr);
  if (ret != ESP_OK) {
    Serial.println("Error calling read_remote_name");
    return false;
  }

  // Wait for the callback (up to 10 seconds)
  unsigned long start = millis();
  while (!scan_finished && (millis() - start < 10000)) {
    delay(100);
  }

  if (!scan_finished) {
    Serial.println(" Timed out waiting for response.");
  }

  return found_device;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n--- 📱 SAMSUNG S25 ULTRA CLASSIC SCANNER (ESP-IDF) ---");

  // Initialize Bluetooth Controller
  if (!btStart()) {
    Serial.println("Failed to start BT controller");
    return;
  }

  // Initialize Bluedroid Stack
  if (esp_bluedroid_init() != ESP_OK) {
    Serial.println("Failed to init Bluedroid");
    return;
  }
  if (esp_bluedroid_enable() != ESP_OK) {
    Serial.println("Failed to enable Bluedroid");
    return;
  }

  // Register GAP Callback
  esp_bt_gap_register_callback(bt_gap_cb);

  Serial.println("Bluetooth Classic (GAP) Initialized.");
}

void loop() {
  Serial.println("\n--- New Check ---");

  if (checkPresence()) {
    Serial.println(">>> ✅ SAMSUNG IS HOME! <<<");
  } else {
    Serial.println(">>> 🚶 SAMSUNG NOT DETECTED <<<");
  }

  Serial.println("[SYSTEM] Cooling down for 5s...");
  delay(5000);
}
