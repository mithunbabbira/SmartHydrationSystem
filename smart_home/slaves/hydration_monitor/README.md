# Hydration Monitor Firmware Logic

This device is a **"Smart Sensor / Dumb Actuator"**. Most logic (Drink detection, Schedules) lives on the Python Server. This firmware focuses on robust hardware handling.

## 🧠 The Algorithm (Loop)

1.  **Weight Sensing (`ScaleManager`)**
    - **Interval:** Every 500ms
    - Reads the HX711 Load Cell.
    - **Tare Logic:** On boot, attempts to load a saved "zero point" from Flash Memory (NVM). If empty, it tares and saves.
    - **Smoothing:** Averages 5 readings to remove noise.

2.  **Telemetry Reporting (`NetworkManager`)**
    - **Interval:** Every 5 seconds
    - Sends a data packet to the Master Gateway via **ESP-NOW**.
    - **Packet Contains:** Current Weight, Weight Delta (Change), Battery Level (future), Alert Status.

3.  **Alert Handling (`AlertManager`)**
    - **Interval:** Continuous (Non-blocking)
    - Checks the requested `alert_level` (received from Server).
    - **Level 1 (Warning):** Blinks Blue RGB + White LED (Legacy) slowly.
    - **Level 2 (Critical):** Blinks Red RGB + White LED (Legacy) fast + Buzzes.
    - **Level 0 (Off):** Silence.

4.  **Command Reception (ESP-NOW Callback)**
    - Listens for packets from Master (ID 0).
    - **Supported Commands:**
        - `TARE`: Re-calibrates zero.
        - `ALERT <lvl>`: Sets alert mode.
        - `SNOOZE`: Temporarily stops alerts locally (though Server manages snooze timing).

## 📂 Modular Structure (New)

- `hydration_monitor.ino`: Setup and Main Loop.
- `ScaleManager.h`: Handles HX711 and EEPROM saving.
- `AlertManager.h`: Handles LEDs and Buzzer patterns.
- `NetworkManager.h`: Handles ESP-NOW sending/receiving.
- `config.h`: Pin definitions and constants.
