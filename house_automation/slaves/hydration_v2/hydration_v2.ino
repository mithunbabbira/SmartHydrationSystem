/*
 * Hydration Slave v2 - Non-blocking, timestamped logging
 *
 * - Time sync: non-blocking in loop(); Pi sends 0x31 once (e.g. at boot). No blocking in setup().
 * - Serial: timestamped logs when HYDRATION_LOG is 1 in Config.h (disable for production).
 * - Loop: processIncomingPackets, timeSync.tick(), sleep/day from timeSync, logic.update().
 */

#include "Config.h"
#include "Log.h"
#include "Comms.h"
#include "Hardware.h"
#include "TimeSync.h"
#include "StateMachine.h"

// --- Globals ---
HydrationHW hw;
Comms comms;
TimeSync timeSync;
StateMachine logic;

// --- Incoming ESP-NOW packet handling ---
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
    hw.setLed(incomingPacket.data > 0);
    break;

  case CMD_SET_BUZZER:
    hw.setBuzzer(incomingPacket.data > 0);
    break;

  case CMD_SET_RGB:
    hw.setRgb((int)incomingPacket.data);
    break;

  case CMD_REPORT_TIME: {
    uint32_t timestamp = incomingPacket.data;
    timeSync.setTimeFromPi(timestamp);
    break;
  }

  case CMD_REPORT_PRESENCE: {
    bool isHome = (incomingPacket.data != 0);
    LOG_INFO2("PRESENCE UPDATE: ", isHome ? "HOME" : "AWAY");
    logic.handlePresence(isHome);
    break;
  }

  case CMD_TARE:
    LOG_INFO("TARE REQUEST RECEIVED");
    hw.tare();
    comms.sendFloat(CMD_REPORT_WEIGHT, 0.0f);
    break;

  case CMD_GET_WEIGHT: {
    float weight = hw.getWeight();
    comms.sendFloat(CMD_REPORT_WEIGHT, weight);
    LOG_INFO2("Sent Weight: ", weight);
    break;
  }

  case CMD_REQUEST_DAILY_TOTAL: {
    float total = logic.getDailyTotal();
    comms.sendFloat(CMD_DAILY_TOTAL, total);
    LOG_INFO2("Sent Daily Total: ", total);
    break;
  }

  default:
    LOG_INFO("Unknown Command");
    break;
  }
}

void setup() {
  Serial.begin(115200);
  LOG_INFO("Hydration Slave v2 Booting...");

  comms.begin();
  delay(100);
  hw.begin();

  timeSync.begin();
  logic.begin(&hw, &comms, &timeSync);

  LOG_INFO("Hydration Slave v2 Ready (non-blocking, time sync in loop)");
}

void loop() {
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

  logic.update();
}
