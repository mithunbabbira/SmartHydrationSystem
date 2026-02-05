/*
 * Hydration Slave v2 - main sketch
 *
 * - Keeps all work in loop() non-blocking for stability.
 * - Uses a one-time time sync from Pi at boot (0x31) and then tracks time locally.
 * - Sends timestamped Serial logs when HYDRATION_LOG is 1 (see Config.h).
 * - Delegates hardware, comms, timekeeping and logic to separate modules.
 */

#include "Config.h"
#include "Log.h"
#include "Comms.h"
#include "Hardware.h"
#include "TimeSync.h"
#include "StateMachine.h"

// --- Globals (single instances of each module) ---
HydrationHW hw;
Comms comms;
TimeSync timeSync;
StateMachine logic;
unsigned long lastWeightReportMs = 0;

// --- Incoming ESP-NOW packet handling ---
// Reads any packet delivered by the ESP-NOW callback and maps it
// to the right hardware / logic action or reply back to the Pi.
void processIncomingPackets() {
  if (!packetReceived)
    return;
  packetReceived = false;

#if HYDRATION_LOG
  Serial.print("[");
  Serial.print(millis());
  Serial.print("] CMD Received: 0x");
  Serial.print(incomingPacket.command, HEX);
  Serial.print(" Data: ");
  Serial.println(incomingPacket.data);
#endif

  switch (incomingPacket.command) {
  case CMD_SET_LED:
    // Directly drive the white status LED from Pi
    hw.setLed(incomingPacket.data > 0);
    break;

  case CMD_SET_BUZZER:
    // Directly drive the buzzer from Pi
    hw.setBuzzer(incomingPacket.data > 0);
    break;

  case CMD_SET_RGB:
    // Directly set RGB color based on numeric code (see Config.h)
    hw.setRgb((int)incomingPacket.data);
    break;

  case CMD_REPORT_TIME: {
    // Pi reports current timestamp (seconds since epoch); TimeSync
    // stores offset so we can calculate hour/day without blocking.
    uint32_t timestamp = incomingPacket.data;
    timeSync.setTimeFromPi(timestamp);
    break;
  }

  case CMD_REPORT_PRESENCE: {
    // Pi replies whether user is HOME / AWAY; state machine
    // decides if reminder should run or snooze.
    bool isHome = (incomingPacket.data != 0);
    LOG_INFO2("PRESENCE UPDATE: ", isHome ? "HOME" : "AWAY");
    logic.handlePresence(isHome);
    break;
  }

  case CMD_TARE:
    // Tare request from Pi: zero the scale and confirm with weight=0
    LOG_INFO("TARE REQUEST RECEIVED");
    hw.tare();
    comms.sendFloat(CMD_REPORT_WEIGHT, 0.0f);
    break;

  case CMD_GET_WEIGHT: {
    // On-demand weight query from Pi (e.g. button on dashboard)
    float weight = hw.getWeight();
    comms.sendFloat(CMD_REPORT_WEIGHT, weight);
    LOG_INFO2("Sent Weight: ", weight);
    break;
  }

  case CMD_REQUEST_DAILY_TOTAL: {
    // Pi asks for current daily total (ml) for display
    float total = logic.getDailyTotal();
    comms.sendFloat(CMD_DAILY_TOTAL, total);
    LOG_INFO2("Sent Daily Total: ", total);
    break;
  }

  default:
    // Unknown or unsupported command (safe to ignore)
    LOG_INFO("Unknown Command");
    break;
  }
}

void setup() {
  // Serial only for debugging; cost is controlled by HYDRATION_LOG
  Serial.begin(115200);
  LOG_INFO("Hydration Slave v2 Booting...");

  // Bring up ESP-NOW, scale, RGB/LED/buzzer, and NVM-backed state
  comms.begin();
  delay(100);
  hw.begin();

  // Prepare time sync state and load last-known hydration state
  timeSync.begin();
  logic.begin(&hw, &comms, &timeSync);

  LOG_INFO("Hydration Slave v2 Ready (non-blocking, time sync in loop)");
}

void loop() {
  // 1) Handle any ESP-NOW packets that arrived since last loop
  processIncomingPackets();

  // Non-blocking time sync: request 0x30 until 0x31 received or timeout
  timeSync.tick(comms);

  // Sleep window and daily reset from time (Pi sends time once at boot; we track locally)
  int hour = timeSync.getHour();
  bool isSleeping = (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR);
  logic.setSleep(isSleeping);

  if (timeSync.isSynced()) {
    unsigned long day = timeSync.getDay();
    logic.checkDay((int)day);
  }

  // 2) Run hydration state machine (monitoring, reminder, missing bottle, etc.)
  logic.update();

  // Periodic weight report for dashboard (same as v1)
  unsigned long now = millis();
  if (now - lastWeightReportMs >= WEIGHT_REPORT_INTERVAL_MS) {
    lastWeightReportMs = now;
    comms.sendFloat(CMD_REPORT_WEIGHT, hw.getWeight());
  }
}
