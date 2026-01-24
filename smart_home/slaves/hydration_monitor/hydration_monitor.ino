/*
 * Smart Home Slave ID 1: Hydration Monitor (Refactored)
 * -----------------------------------------------------
 * Modular Architecture using Managers.
 */

#include "../../master_gateway/protocol.h"
#include "config.h"

// Modules
#include "AlertManager.h"
#include "CommsManager.h"
#include "ScaleManager.h"

// --- Globals ---
AlertManager alertMgr;
ScaleManager scaleMgr;
CommsManager netMgr;

// --- State ---
float current_weight = 0;
float previous_weight = 0;
float weight_delta = 0;
bool is_missing = false;

// --- Timing ---
unsigned long last_send_time = 0;
unsigned long last_weight_check = 0;

// --- ESP-NOW Callback ---
void onDataRecv(const esp_now_recv_info *recv_info, const uint8_t *data,
                int len) {
  if (len < sizeof(ESPNowHeader))
    return;
  ESPNowHeader *header = (ESPNowHeader *)data;

  // Security: Check master?
  if (netMgr.master_known &&
      memcmp(recv_info->src_addr, netMgr.master_mac, 6) != 0)
    return;

  if (header->msg_type == MSG_TYPE_COMMAND) {
    // 1. Generic Command (Alert, Tare, Snooze)
    // Structure: Header(3) + CmdID(1) + Val(4)
    if (len >= sizeof(GenericCommand)) {
      GenericCommand *g = (GenericCommand *)data;

      if (g->command_id == 1) { // TARE
        scaleMgr.tare();
      } else if (g->command_id == 2) { // SNOOZE
        alertMgr.setLevel(0);
      } else if (g->command_id == 3) { // ALERT
        alertMgr.setLevel(g->val);     // val is uint32_t, level is uint8_t
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize Modules
  alertMgr.begin();
  netMgr.begin();   // Sets up WiFi/ESP-NOW
  scaleMgr.begin(); // Sets up HX711 (with timeout)

  // Register Callback
  esp_now_register_recv_cb(onDataRecv);

  Serial.println("Hydration Monitor (Refactored) Ready.");
}

void loop() {
  unsigned long now = millis();

  // 1. Measured Weight
  if (now - last_weight_check > WEIGHT_SAMPLE_INTERVAL) {
    last_weight_check = now;
    current_weight = scaleMgr.readWeight();

    // Basic Logic
    is_missing = (current_weight < WEIGHT_MISSING_THRESHOLD);
    weight_delta = current_weight - previous_weight;
    previous_weight = current_weight;
  }

  // 2. Report Telemetry
  if (now - last_send_time > TELEMETRY_INTERVAL) {
    last_send_time = now;
    netMgr.sendTelemetry(current_weight, weight_delta, alertMgr.currentLevel,
                         is_missing);
  }

  // 3. Update Alerts
  alertMgr.update();
}
