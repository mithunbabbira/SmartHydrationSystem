# MQTT IR Transmitter

This project allows an ESP8266 to transmit IR codes received via MQTT from a Raspberry Pi server.

## Hardware Setup
- **Microcontroller**: NodeMCU ESP8266
- **IR Transmitter**: Connect to **Pin D2** (GPIO 4)
- **Power**: USB or 3.3V/5V

## Software Setup
1. **Upload Code**:
   - Open `ir_mqtt_transmitter.ino` in Arduino IDE.
   - Install required libraries (`PubSubClient`, `IRremoteESP8266`).
   - Select your Board (NodeMCU 1.0) and Port.
   - Upload.

2. **Server Update**:
   - The `hydration_server.py` has been updated to support IR commands.
   - Restart the server if it's running.

## Usage
1. **Start Server**: `python3 hydration_server.py`
2. **Send Command**:
   - In the server console, type: `ir <HEX_CODE>`
   - Example: `ir 0xF7F00F`
3. **Verify**:
   - The ESP8266 should blink/transmit.
   - Serial Monitor on ESP8266 will confirm reception: `Transmitting NEC: 0xF7F00F`.
