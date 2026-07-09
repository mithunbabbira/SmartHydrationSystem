/*
 * Hydration Slave - Modular structure
 *
 * Sections:
 * 1. Time (RTC from Pi; used for sleep window and daily reset)
 * 2. Incoming packet handling (ESP-NOW -> processIncomingPackets)
 * 3. Time sync (blocking with timeout; if Pi doesn't reply in 60s, proceed without time)
 * 4. Setup / Loop
 *
 * Time sync flow: Slave sends CMD_REQUEST_TIME (0x30) to Master -> Pi.
 * Pi replies with CMD_REPORT_TIME (0x31) + timestamp to this slave's MAC.
 * If no reply within TIME_SYNC_TIMEOUT_MS, we continue anyway (getHour() returns 12, daily reset disabled).
 */

#include "Hardware.h"
#include "LogicManager.h"
#include "SlaveComms.h"
#include "SlaveConfig.h"

// --- Globals ---
SlaveComms comms;
HydrationHW hw;
LogicManager logic;

unsigned long rtcOffset = 0;
bool timeSynced = false;

// --- Section 1: Time (for sleep and daily reset) ---
int getHour() {
  if (!timeSynced)
    return 12;
  unsigned long currentEpoch = rtcOffset + (millis() / 1000);
  currentEpoch += 19800;  // GMT+5.5
  return (currentEpoch % 86400L) / 3600;
}

// --- Section 2: Incoming ESP-NOW packet handling ---
void processIncomingPackets() {
  if (!packetReceived)
    return;
  packetReceived = false;

  Serial.print("CMD Received: 0x");
  Serial.print(incomingPacket.command, HEX);
  Serial.print(" Data: ");
  Serial.println(incomingPacket.data);

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
    Serial.print("TIME SYNC Received: ");
    Serial.println(timestamp);
    rtcOffset = timestamp - (millis() / 1000);
    timeSynced = true;
    break;
  }

  case CMD_REPORT_PRESENCE: {
    bool isHome = (incomingPacket.data != 0);
    Serial.print("PRESENCE UPDATE: ");
    Serial.println(isHome ? "HOME" : "AWAY");
    logic.handlePresence(isHome);
    break;
  }

  case CMD_TARE:
    Serial.println("TARE REQUEST RECEIVED");
    hw.tare();
    comms.sendFloat(CMD_REPORT_WEIGHT, 0.0);
    break;

  case CMD_GET_WEIGHT: {
    float weight = hw.getWeight();
    comms.sendFloat(CMD_REPORT_WEIGHT, weight);
    Serial.print("Sent Weight: ");
    Serial.println(weight);
    break;
  }

  case CMD_REQUEST_DAILY_TOTAL: {
    float total = logic.getDailyTotal();
    comms.sendFloat(CMD_DAILY_TOTAL, total);
    Serial.print("Sent Daily Total: ");
    Serial.println(total);
    break;
  }

  default:
    Serial.println("Unknown Command");
    break;
  }
}

// --- Section 3: Time sync (blocking with timeout) ---
void waitForTimeSync() {
  Serial.println("Waiting for Time Sync from Pi...");
  unsigned long lastRequest = 0;
  unsigned long syncStart = millis();

  while (!timeSynced) {
    unsigned long now = millis();

    // Timeout: proceed without time so slave is not stuck forever
    if (now - syncStart >= TIME_SYNC_TIMEOUT_MS) {
      Serial.println("Time sync timeout (60s) - continuing without time. Check Master MAC and Pi serial.");
      timeSynced = false;
      break;
    }

    hw.animateRainbow(10);

    if (now - lastRequest >= TIME_SYNC_REQUEST_MS) {
      lastRequest = now;
      Serial.println("Requesting Time...");
      comms.send(CMD_REQUEST_TIME, 0);
    }

    processIncomingPackets();
    delay(5);
  }

  if (timeSynced) {
    Serial.println("Time Synced! Starting Main Logic.");
  }
  hw.setRgb(0);
}

// --- Section 4: Setup ---
void setup() {
  Serial.begin(115200);

  comms.begin();
  delay(100);
  hw.begin();

  Serial.println("Hydration Slave Booting...");
  waitForTimeSync();
  logic.begin(&hw, &comms);
  Serial.println("Hydration Slave Ready (Modular Framework)");
}

// --- Section 4: Loop ---
void loop() {
  int hour = getHour();
  bool isSleeping = (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR);
  logic.setSleep(isSleeping);

  if (timeSynced) {
    int day = (rtcOffset + (millis() / 1000) + 19800) / 86400;
    logic.checkDay(day);
  }

  logic.update();
  processIncomingPackets();

  static unsigned long lastReportTime = 0;
  if (millis() - lastReportTime >= 5000) {
    lastReportTime = millis();
    comms.sendFloat(CMD_REPORT_WEIGHT, hw.getWeight());
  }
}
