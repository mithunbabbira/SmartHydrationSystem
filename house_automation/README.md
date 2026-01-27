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

## Hardware Connection

### Option 1: USB (Recommended)
Connect the **Master ESP32** to the **Raspberry Pi** using a high-quality **Micro-USB cable**.
- **Pros**: Handles both power and data; easiest setup.
- **Port**: Typically `/dev/ttyUSB0` or `/dev/ttyACM0` on the Pi.

### Option 2: UART (Direct GPIO)
Connect the UART pins directly. **Common Ground is essential.**

| ESP32 Pin | Raspberry Pi Pin | Function |
|-----------|------------------|----------|
| TX (GPIO1)| RX (GPIO15 / Pin 10)| Data from ESP -> Pi |
| RX (GPIO3)| TX (GPIO14 / Pin 8) | Data from Pi -> ESP |
| GND       | GND (Pin 6)       | Common Ground |

*Note: The current `master_esp32.ino` uses the standard `Serial` object, which is routed to the USB port and usually GPIO1/3. If using USB, do not connect GPIO1/3 to anything else.*

## Managing the Auto-Start Service

The system includes a service file `pi_controller/smart-home.service` to run the dashboard automatically on boot.

### Installation
```bash
sudo cp house_automation/pi_controller/smart-home.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable smart-home.service
sudo systemctl start smart-home.service
```

### Management Commands
```bash
# Start/Stop
sudo systemctl start smart-home.service
sudo systemctl stop smart-home.service

# Check Status (Logs)
sudo systemctl status smart-home.service

# Restart (After git pull)
sudo systemctl restart smart-home.service
```

### Bluetooth Requirement
This system uses `l2ping` (part of `bluez`) for presence detection.
```bash
sudo apt-get install bluez
```

