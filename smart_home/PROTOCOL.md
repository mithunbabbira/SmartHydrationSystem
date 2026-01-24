# Communication Protocol Spec

This document defines the communication between Slaves, Master Gateway, and the Pi5 Server.

## 1. Slave ↔ Master (ESP-NOW)

All messages are binary structs.

### Common Header
```cpp
struct ESPNowHeader {
    uint8_t slave_id;   // 1=Hydration, 2=LED, 3=IR
    uint8_t msg_type;   // 1=Telemetry, 2=Command, 3=Ack
};
```

### Telemetry (Slave → Master)
Each slave sends its own specific data.

**ID 1: Hydration**
```cpp
struct HydrationTelemetry {
    float weight;
    float delta;
    uint8_t alert_level;
    bool bottle_missing;
};
```

**ID 2: LED Controller**
```cpp
struct LEDStatus {
    bool is_on;
    uint8_t r, g, b;
    uint8_t mode;
};
```

## 2. Master ↔ Pi5 (UART)

Format: **JSON Lines**
Baud Rate: **115200**

### From Master to Pi5 (Telemetry / Events)
```json
{"src": 1, "type": "telemetry", "data": {"weight": 500.5, "delta": 10.0, "alert": 0, "missing": false}}
{"src": 2, "type": "status", "data": {"on": true, "color": "FF0000", "mode": 37}}
```

### From Pi5 to Master (Commands)
```json
{"dst": 1, "cmd": "tare"}
{"dst": 2, "cmd": "set_color", "val": "00FF00"}
{"dst": 3, "cmd": "ir_send", "val": "0xF7F00F"}
```

## 3. Presence Detection (Pi5 Native)

Pi5 handles presence using `hcitool name <MAC>`.
Presence state is injected into the server logic and can be forwarded to slaves if needed:
```json
{"dst": 1, "cmd": "presence", "val": "home"}
```
