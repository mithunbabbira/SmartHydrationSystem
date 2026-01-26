#include "Hardware.h"
#include "LogicManager.h" // Includes Hardware and SlaveComms
#include "SlaveComms.h"
#include "SlaveConfig.h" // Includes Sleep Config

SlaveComms comms;
HydrationHW hw;
LogicManager logic;

// Time Tracking
unsigned long rtcOffset = 0;
bool timeSynced = false;

// Config (Mirrored from Master config.h) - NOW IN SlaveConfig.h
// #define SLEEP_START_HOUR 23
// #define SLEEP_END_HOUR 10

int getHour() {
  if (!timeSynced)
    return 12; // Default to noon if no time
  unsigned long currentEpoch = rtcOffset + (millis() / 1000);
  // Manual GMT+5.5 adjustment (19800 seconds)
  currentEpoch += 19800;
  return (currentEpoch % 86400L) / 3600;
}

// Helper to process any received ESP-NOW packets
void processIncomingPackets() {
  if (packetReceived) {
    packetReceived = false;

    // Debug Log
    Serial.print("CMD Received: 0x");
    Serial.print(incomingPacket.command, HEX);
    Serial.print(" Data: ");
    Serial.println(incomingPacket.data);

    // Execute Command
    switch (incomingPacket.command) {
    case CMD_SET_LED:
      hw.setLed(incomingPacket.data > 0);
      break;

    case CMD_SET_BUZZER:
      hw.setBuzzer(incomingPacket.data > 0);
      break;

    case CMD_SET_RGB: {
      hw.setRgb((int)incomingPacket.data);
      break;
    }

    case CMD_REPORT_TIME: {
      uint32_t timestamp = incomingPacket.data;
      Serial.print("TIME SYNC Received: ");
      Serial.println(timestamp);

      // Update internal time tracking
      rtcOffset = timestamp - (millis() / 1000);
      timeSynced = true;
      break;
    }

    case CMD_REPORT_PRESENCE: {
      // Data might be sent as float (1.0 = 0x3F800000) or int (1)
      // Any non-zero value is treated as HOME.
      bool isHome = (incomingPacket.data != 0);
      Serial.print("PRESENCE UPDATE: ");
      Serial.println(isHome ? "HOME"
                            : "AWAY (Raw Data: " + String(incomingPacket.data) +
                                  ")");

      // Notify Logic Manager
      logic.handlePresence(isHome);
      break;
    }

    case CMD_TARE: {
      Serial.println("TARE REQUEST RECEIVED");
      hw.tare(); // Zero and save to NVM
      // Send fresh zero weight immediately
      comms.sendFloat(CMD_REPORT_WEIGHT, 0.0);
      break;
    }

    case CMD_GET_WEIGHT: {
      float weight = hw.getWeight();
      // Send raw bits of float
      comms.sendFloat(CMD_REPORT_WEIGHT, weight);
      Serial.print("Sent Weight: ");
      Serial.println(weight);
      break;
    }

    default:
      Serial.println("Unknown Command");
      break;
    }
  }
}

void waitForTimeSync() {
  Serial.println("Waiting for Time Sync from Pi...");
  unsigned long lastRequest = 0;

  while (!timeSynced) {
    // 1. Animate Rainbow (Visual Feedback)
    hw.animateRainbow(10); // Update every 10ms

    // 2. Request Time every 5 seconds
    if (millis() - lastRequest > 5000) {
      lastRequest = millis();
      Serial.println("Requesting Time...");
      comms.send(CMD_REQUEST_TIME, 0);
    }

    // 3. Check for Response
    processIncomingPackets();

    // Tiny delay to yield
    delay(5);
  }

  Serial.println("Time Synced! Starting Main Logic.");
  hw.setRgb(0); // Turn off Rainbow
}

void setup() {
  Serial.begin(115200);

  // Init WiFi/EspNow FIRST to avoid pin conflicts (Fixes LED flicker)
  comms.begin();
  delay(100);

  // THEN init Hardware (Pins) to ensure they stay set
  hw.begin();

  Serial.println("Hydration Slave Booting...");

  // BLOCKING: Wait for Time Sync before starting logic
  waitForTimeSync();

  // Init Logic State Machine
  logic.begin(&hw, &comms);

  Serial.println("Hydration Slave Ready (Modular Framework)");
}

void loop() {
  // Update Sleep State
  int hour = getHour();
  bool isSleeping = (hour >= SLEEP_START_HOUR || hour < SLEEP_END_HOUR);
  logic.setSleep(isSleeping);

  // Update Logic (Bottle Detection, Alerts)
  logic.update();

  // Check for incoming commands
  processIncomingPackets();

  // Periodic Reporting (Every 5 Seconds)
  static unsigned long lastReportTime = 0;
  if (millis() - lastReportTime >= 5000) {
    lastReportTime = millis();
    float weight = hw.getWeight();
    comms.sendFloat(CMD_REPORT_WEIGHT, weight);
    // Serial.print("Auto-Report Weight: "); Serial.println(weight);
  }
}
