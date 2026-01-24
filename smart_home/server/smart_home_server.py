#!/usr/bin/env python3
"""
Smart Home Central Server (Pi5) - Modular Architecture
======================================================
Coordinates Services: Gateway, Hydration, Presence.
"""

import time
import threading
import json
import logging
from datetime import datetime
import server_config as config

# Import Services
from services.gateway import GatewayService
from services.hydration import HydrationService
from services.presence import PresenceService

# Timestamp helper
def log(msg):
    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    print(f"[{timestamp}] {msg}")

# --- Globals ---
gateway = None
hydration = None
presence = None
latest_telemetry = {}

def on_telemetry(src, data):
    """Callback when Valid Telemetry is received"""
    global latest_telemetry
    latest_telemetry[src] = data
    
    # Forward to Hydration Service
    if src == 1:  # Hydration Monitor
        hydration.process_update(
            weight=data.get("weight", 0),
            is_missing=data.get("is_missing", False),
            alert_level=data.get("alert_level", 0)
        )

def on_presence_change(is_home):
    """Callback when presence changes"""
    log(f"Presence Change Detected: {'HOME' if is_home else 'AWAY'}")
    # Send to all slaves if needed

def main():
    global gateway, hydration, presence
    
    log("Smart Home Server Starting (Modular v3)...")
    
    # Initialize Services
    presence = PresenceService(on_change_callback=on_presence_change)
    hydration = HydrationService(presence)
    gateway = GatewayService(on_telemetry_callback=on_telemetry)
    
    # Start Services
    presence.start()
    gateway.start()
    
    log("Services Started. Searching for Master Gateway...")
    
    # Interactive CLI
    log("") 
    log("Commands: help, stats, tare, led <on/off/mode>, alert <lvl>, snooze, quit")
    
    while True:
        try:
            cmd_input = input("smart_home> ").strip().lower().split()
            if not cmd_input:
                continue
            
            cmd = cmd_input[0]
            
            if cmd == "quit" or cmd == "exit":
                log("Shutting down...")
                presence.stop()
                gateway.stop()
                break
                
            elif cmd == "help":
                log("Available commands:")
                log("  help          - Show this help")
                log("  stats         - Show system stats")
                log("  tare          - Tare the scale") 
                log("  snooze        - Snooze hydration alerts")
                log("  alert <lvl>   - Set alert level (0-2)")
                log("  led <cmd>     - Control LED Strip (on/off/red/blue...)")
                log("  ir <code>     - Send IR Code")
                
            elif cmd == "presence":
                log(f"Presence State: {'Home' if presence.is_home else 'Away'}")
                log(f"Device MAC: {config.PHONE_MAC}")

            elif cmd == "stats":
                log(f"Presence: {'HOME' if presence.is_home else 'AWAY'}")
                log(f"Hydration: Today={hydration.today_consumption}ml Alert={hydration.alert_level}")
                log(f"Latest Telemetry: {json.dumps(latest_telemetry, indent=2)}")
            
            elif cmd == "tare":
                log("Sending TARE command to Hydration Monitor...")
                gateway.send_command(1, "tare", 0)
            
            elif cmd == "snooze":
                log("Sending SNOOZE command...")
                gateway.send_command(1, "snooze", 0)
            
            elif cmd == "alert" and len(cmd_input) > 1:
                level = int(cmd_input[1])
                log(f"Sending ALERT command (Level {level})...")
                gateway.send_command(1, "alert", level)
            
            elif cmd == "led" and len(cmd_input) > 1:
                led_cmd = cmd_input[1]
                log(f"Sending LED command: {led_cmd}")
                
                # Map common commands
                cmd_map = {
                    "on": {"cmd": "on"},
                    "off": {"cmd": "off"},
                    "red": {"cmd": "color", "r": 255, "g": 0, "b": 0},
                    "green": {"cmd": "color", "r": 0, "g": 255, "b": 0},
                    "blue": {"cmd": "color", "r": 0, "g": 0, "b": 255},
                    "white": {"cmd": "color", "r": 255, "g": 255, "b": 255},
                    "purple": {"cmd": "color", "r": 128, "g": 0, "b": 128},
                    "yellow": {"cmd": "color", "r": 255, "g": 255, "b": 0},
                    "cyan": {"cmd": "color", "r": 0, "g": 255, "b": 255},
                }
                
                if led_cmd in cmd_map:
                    gateway.send_command(2, "led", cmd_map[led_cmd])
                else:
                    # Try as effect
                    gateway.send_command(2, "led", {"cmd": led_cmd})
            
            elif cmd == "ir" and len(cmd_input) > 1:
                code = int(cmd_input[1], 16)
                log(f"Sending IR code: 0x{code:X}")
                gateway.send_command(3, "ir", code)
            
            else:
                log(f"Unknown command: {cmd}")
                
        except KeyboardInterrupt:
            log("\nShutting down...")
            presence.stop()
            gateway.stop()
            break
        except Exception as e:
            log(f"Error: {e}")

if __name__ == "__main__":
    main()
