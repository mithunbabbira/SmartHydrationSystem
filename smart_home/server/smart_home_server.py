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
import server_config as config

# Import Services
from services.gateway import GatewayService
from services.hydration import HydrationService
from services.presence import PresenceService

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
    if src == 1 and "weight" in data:
        # Check current presence state
        is_home = presence.is_home if presence else False
        is_missing = data.get("missing", False)
        hydration.process_weight(data["weight"], is_home, is_missing)

def on_presence_change(is_home):
    """Callback when User Presence changes"""
    # Tell Slaves
    if gateway:
        gateway.send_command(1, "presence", "home" if is_home else "away")
        # gateway.send_command(2, "presence", ...) # LED slave might care?

def main():
    global gateway, hydration, presence
    
    print("Smart Home Server Starting (Modular v3)...")
    
    # 1. Initialize Services
    gateway = GatewayService(on_telemetry_callback=on_telemetry)
    # Hydration needs a way to send commands -> Gateway.send_command
    hydration = HydrationService(send_command_callback=gateway.send_command)
    presence = PresenceService(on_change_callback=on_presence_change)
    
    # 2. Start Background Threads
    threading.Thread(target=gateway.start_reader, daemon=True).start()
    presence.start()
    
    print("Services Started. Searching for Master Gateway...")
    
    # 3. CLI Loop
    print("\nCommands: help, stats, tare, led <on/off/mode>, alert <lvl>, snooze, quit")
    
    try:
        while True:
            cmd_input = input("smart_home> ").strip().lower().split()
            if not cmd_input: continue
            
            cmd = cmd_input[0]
            
            if cmd == "quit": 
                gateway.stop()
                presence.stop()
                break
                
            elif cmd == "help":
                print("\n📚 Available Commands:")
                print("  stats         - Show telemetry & presence")
                print("  tare          - Tare Hydration Scale")
                print("  snooze        - Snooze Alerts (30m)")
                print("  alert <lvl>   - Test Alert (1=LED, 2=Buzz)")
                print("  led <cmd>     - Control LED Strip (on/off/red/blue...)")
                print("  ir <code>     - Send IR Code")
                
            elif cmd == "presence":
                print(f"Presence State: {'Home' if presence.is_home else 'Away'}")
                print(f"Device MAC: {config.PHONE_BT_MAC}")

            elif cmd == "stats":
                print(f"Presence: {'HOME' if presence.is_home else 'AWAY'}")
                print(f"Hydration: Today={hydration.today_consumption}ml Alert={hydration.alert_level}")
                print(f"Telemetry: {json.dumps(latest_telemetry, indent=2)}")
                
            elif cmd == "led":
                if len(cmd_input) < 2:
                    print("Usage: led <on/off/red/blue/green/flash>")
                    continue
                sub = cmd_input[1]
                
                details = {"on": True, "mode": 0, "speed": 50, "r": 0, "g": 0, "b": 0}
                if sub == "off": details["on"] = False
                elif sub == "on": details["on"] = True
                elif sub == "red": details.update({"r": 255})
                elif sub == "green": details.update({"g": 255})
                elif sub == "blue": details.update({"b": 255})
                elif sub == "flash": details.update({"mode": 2, "speed": 200})
                
                gateway.send_command(2, "led", details)
                
            elif cmd == "alert":
                try:
                    lvl = int(cmd_input[1]) if len(cmd_input) > 1 else 1
                    gateway.send_command(1, "alert", {"level": lvl})
                    print(f"Triggered Alert Level {lvl}")
                except: print("Invalid level")

            elif cmd == "tare":
                gateway.send_command(1, "tare", None)
                print("Sent Tare Command")

            elif cmd == "snooze":
                hydration.snooze(30)
                
            elif cmd == "ir":
                 if len(cmd_input) > 1:
                     gateway.send_command(3, "ir", {"code": cmd_input[1]})
            
    except KeyboardInterrupt:
        print("\nStopping...")
        gateway.stop()
        presence.stop()

if __name__ == "__main__":
    main()
