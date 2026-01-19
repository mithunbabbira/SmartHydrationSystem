# ESP8266 IR Receiver & Stubbed Transmitter

## Hardware Setup
- **Microcontroller**: NodeMCU ESP8266
- **IR Receiver**: Connect to **Pin D5** (GPIO 14)
   - Signal -> D5
   - VCC    -> 3.3V
   - GND    -> GND
- **IR Transmitter**: *Not connected* (simulated in software)

## Instructions
1. **Upload** the `ir_remote.ino` sketch to your ESP8266.
2. Open the **Serial Monitor** (set baud rate to **9600**).
3. **Receive Signals**:
   - Point any IR remote (TV, AC, etc.) at the sensor.
   - Press a button.
   - You should see the `Protocol`, `Code`, and `Bits` displayed in the Serial Monitor.
4. **Simulate Transmission**:
   - Type `t` in the Serial Monitor input and press Send/Enter.
   - The code will print what *would* be transmitted if a transmitter were attached.

## Troubleshooting
- If you see `Protocol: UNKNOWN`, try a different remote or move closer.
- Ensure your wiring corresponds to the pin definitions in the code (`IR_RECV_PIN = 14` / D5).
