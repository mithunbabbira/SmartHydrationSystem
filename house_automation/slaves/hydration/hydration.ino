#include "Hardware.h"
#include "SlaveComms.h"

SlaveComms comms;
HydrationHW hw;

void setup() {
  Serial.begin(115200);

  // Init WiFi/EspNow FIRST to avoid pin conflicts (Fixes LED flicker)
  comms.begin();
  delay(100);

  // THEN init Hardware (Pins) to ensure they stay set
  hw.begin();

  Serial.println("Hydration Slave Ready (Modular Framework)");

  // Request Time on Boot
  Serial.println("Requesting Time...");
  comms.send(CMD_GET_TIME, 0);
}

void loop() {
  // Check for incoming commands
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

    case CMD_SET_TIME: {
      uint32_t timestamp = incomingPacket.data;
      Serial.print("TIME SYNC Received: ");
      Serial.println(timestamp);
      // Time usage: timestamp is Unix Epoch seconds
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
