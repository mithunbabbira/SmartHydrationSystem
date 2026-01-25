# House Automation System

This project contains the firmware and software for a home automation system using ESP-NOW for device communication and a Raspberry Pi as the central controller.

## Project Structure

```
house_automation/
├── master_esp32/       # Firmware for the Master ESP32 Gateway
│   └── master_esp32.ino
└── README.md           # This documentation
```

## Architecture

1.  **Slaves (ESP32/ESP8266)**:
    - Send sensor data to the Master via ESP-NOW.
    - Receive commands from the Master via ESP-NOW.

2.  **Master Node (ESP32)**:
    - Acts as a bridge.
    - **Receives** packet from Slave -> Forwards to Pi via Serial.
    - **Receives** command from Pi via Serial -> Forwards to Slave via ESP-NOW.

3.  **Raspberry Pi**:
    - Central Logic.
    - Connected to Master ESP32 via USB Serial.

## Communication Protocol

### Serial (Pi <-> Master)

**Baud Rate**: 115200

**From Master to Pi (RX):**
When the Master receives data from a slave, it sends a line to the Pi:
`RX:<SLAVE_MAC_ADDRESS>:<PAYLOAD>`

**From Pi to Master (TX):**
To send data to a slave, the Pi sends a line to the Master:
`TX:<TARGET_MAC_ADDRESS>:<PAYLOAD>`

*Note: MAC Address format is XX:XX:XX:XX:XX:XX (e.g., 24:6F:28:A1:B2:C3)*
