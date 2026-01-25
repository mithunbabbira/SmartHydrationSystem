#include "Hardware.h"
#include "SlaveComms.h"

SlaveComms comms;
HydrationHW hw;

void setup() {
  Serial.begin(115200);

  hw.begin();
  comms.begin();

  Serial.println("Hydration Slave Ready (Modular Framework)");
}

void loop() {
  // Check for incoming commands
  if (packetReceived) {
    packetReceived = false;

    // Debug Log
    Serial.print("CMD Received: 0x");
    Serial.print(incomingPacket.command, HEX);
    Serial.print(" Val: ");
    Serial.println(incomingPacket.value);

    // Execute Command
    switch (incomingPacket.command) {
    case CMD_SET_LED:
      hw.setLed(incomingPacket.value > 0); // 1.0 = ON, 0.0 = OFF
      break;

    case CMD_SET_BUZZER:
      hw.setBuzzer(incomingPacket.value > 0);
      break;

    case CMD_SET_RGB:
      hw.setRgb((int)incomingPacket.value);
      break;

    case CMD_GET_WEIGHT: {
      float weight = hw.getWeight();
      comms.send(CMD_REPORT_WEIGHT, weight);
      Serial.print("Sent Weight: ");
      Serial.println(weight);
      break;
    }

    default:
      Serial.println("Unknown Command");
      break;
    }
  }

  // No random sending anymore - purely request/response driven for weight.
  // Or could add periodic reporting if desired.
}
