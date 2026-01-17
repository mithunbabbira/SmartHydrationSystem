# Smart Hydration System

An ESP32-based hydration reminder system that monitors water consumption using a high-precision scale and provides intelligent reminders.

## Features

- **High-Precision Monitoring**: Real-time weight tracking with HX711 and 5kg load cell.
- **2-Level Alerts**: Smart notifications with blinking white LEDs and a buzzer.
- **Bottle Missing Detection**: Alerts you if the bottle is removed and not replaced within 3 minutes.
- **Instant Response**: Alerts stop immediately when the bottle is picked up.
- **NVM Persistence**: Remembers scale calibration even after power cycles.
- **MQTT Integration**: Sends telemetry, consumption data, and alerts to a central server (e.g., Raspberry Pi).
- **RGB Status**: Visual feedback for all system states (Drinking, Resting, Alerting, etc.).

## Project Structure

- `SmartHydrationSystem/`: ESP32 Firmware and configuration.
- `component_test/`: Interactive test sketch for individual hardware components.
- `hydration_server.py`: Python-based MQTT server and command-line interface.

## Quick Start

1.  Configure your WiFi and MQTT settings in `SmartHydrationSystem/config.h`.
2.  Flash the firmware using Arduino IDE or PlatformIO.
3.  Run the Python server on your Raspberry Pi: `python hydration_server.py`.

## Credits

Designed and programmed by NC23896-Mithun.
