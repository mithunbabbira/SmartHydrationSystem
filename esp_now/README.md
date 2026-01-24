# ESP-NOW Master-Slave Arduino Sketches

Simple Arduino code for ESP32 ESP-NOW communication testing.

## Files
- `master.ino` - Upload to 1x ESP32 (master)
- `slave.ino` - Upload to 5x ESP32/ESP32-CAM (slaves)

## Arduino IDE Setup

### 1. Install ESP32 Board Support
- Open Arduino IDE
- File → Preferences → Additional Board Manager URLs:
  ```
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
  ```
- Tools → Board → Boards Manager → Search "ESP32" → Install

### 2. Upload Master
1. Open `master.ino` in Arduino IDE
2. Select: Tools → Board → ESP32 Dev Module
3. Select: Tools → Port → (your ESP32 port)
4. Click Upload
5. Open Serial Monitor (115200 baud)

### 3. Upload Slave
1. Open `slave.ino` in Arduino IDE
2. Select: Tools → Board → ESP32 Dev Module (or AI Thinker ESP32-CAM)
3. Select: Tools → Port → (your ESP32 port)
4. Click Upload
5. Repeat for all 5 slaves

## Expected Output

**Master Serial Monitor:**
```
✓ Slave [0] registered: AA:BB:CC:DD:EE:FF (Slave-EEFF)
📩 PONG [0] Slave-EEFF | Counter: 10 | RTT: 15 ms | RSSI: -32 dBm | Success: 100.0%
```

**Slave Serial Monitor:**
```
✓ Master registered: 11:22:33:44:55:66
📩 PING [counter: 10] | RSSI: -32 dBm
📤 PONG sent [counter:10] | Heap: 250 KB
```

## Features
✅ 1-second ping interval
✅ Automatic registration
✅ RSSI monitoring
✅ Counter tracking
✅ RTT measurement
✅ Success rate calculation

## Troubleshooting
- **No output**: Check baud rate is 115200
- **Upload failed (ESP32-CAM)**: Use USB-to-Serial adapter, hold BOOT button during upload
- **Slave doesn't register**: Power cycle both devices
