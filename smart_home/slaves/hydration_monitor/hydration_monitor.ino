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

  if (netMgr.master_known &&
      memcmp(recv_info->src_addr, netMgr.master_mac, 6) != 0)
    return;

  if (header->msg_type == MSG_TYPE_COMMAND) {
    if (len >= sizeof(GenericCommand)) {
      GenericCommand *g = (GenericCommand *)data;

      if (g->command_id == 1) { // TARE
        scaleMgr.tare();
      } else if (g->command_id == 2) { // SNOOZE
        alertMgr.setLevel(0);
      } else if (g->command_id == 3) { // ALERT
        alertMgr.setLevel(g->val);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize Modules
  alertMgr.begin();
  netMgr.begin();
  scaleMgr.begin();

  // Register Callback
  esp_now_register_recv_cb(onDataRecv);

  Serial.println("Hydration Monitor (Refactored) Ready.");
}

void loop() {
  unsigned long now = millis();

  // 1. Measured Weight (Fast 500ms loop)
  if (now - last_weight_check > WEIGHT_SAMPLE_INTERVAL) {
    last_weight_check = now;
    current_weight = scaleMgr.readWeight();

    bool currently_missing = (current_weight < WEIGHT_MISSING_THRESHOLD);

    // 1a. Immediate Local Feedback (Hybrid Logic)
    if (currently_missing && !is_missing) {
      Serial.println("⚠ Bottle Removed: Local Alert 2");
      alertMgr.setLevel(2);
    } else if (!currently_missing && is_missing) {
      Serial.println("✓ Bottle Replaced: Local Silence");
      alertMgr.setLevel(0);
    }

    is_missing = currently_missing;
    weight_delta = current_weight - previous_weight;
    previous_weight = current_weight;
  }

  // 2. Report Telemetry (Slower 5s loop)
  if (now - last_send_time > TELEMETRY_INTERVAL) {
    last_send_time = now;
    netMgr.sendTelemetry(current_weight, weight_delta, alertMgr.currentLevel,
                         is_missing);
  }

  // 3. Update Alerts
  alertMgr.update();
}
