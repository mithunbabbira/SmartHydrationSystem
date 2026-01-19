# MQTT IR Transmitter & Relay Control

This project allows an ESP8266 to:
1. Transmit IR codes received via MQTT.
2. Control 4 Relays via MQTT.
3. Transmit a boot IR signal (ON) at startup.

## Hardware Setup
- **Microcontroller**: NodeMCU ESP8266
- **IR Transmitter**: **D2** (GPIO 4)
- **4-Channel Relay**:
    - Relay 1: **D1** (GPIO 5)
    - Relay 2: **D5** (GPIO 14)
    - Relay 3: **D6** (GPIO 12)
    - Relay 4: **D7** (GPIO 13)
    - *Note: Logic is Active LOW (LOW = ON)*

## Software Setup
1. **Upload Code**:
   - Open `ir_mqtt_transmitter.ino`.
   - Install libraries (`PubSubClient`, `IRremoteESP8266`).
   - Upload to NodeMCU.

2. **Server Update**:
   - `hydration_server.py` now supports the `relay` command.

## Usage
1. **Start Server**: `python3 hydration_server.py`
2. **IR Command**:
   - `ir 0xF7F00F` -> Transmits code.
3. **Relay Command**:
   - `relay 1 on` -> Turns Relay 1 ON.
   - `relay 3 off` -> Turns Relay 3 OFF.
4. **Boot Behavior**:
   - Immediately upon power-up, it sends `0xF7F00F` via IR.
