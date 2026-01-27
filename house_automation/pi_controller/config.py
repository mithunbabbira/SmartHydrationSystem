import os

# House Automation Configuration

# MAC Registry
# Format: 'FRIENDLY_NAME': 'MAC_ADDRESS'
SLAVE_MACS = {
    'hydration': 'F0:24:F9:0C:DE:54',
    'led_ble': 'A0:A3:B3:2A:20:C0', 
    'ir_remote': 'A0:A3:B3:2A:20:C0',
    'my_phone': '48:EF:1C:49:6A:E7'
}

# Default Port
SERIAL_PORT = '/dev/serial0'

# Adafruit IO Configuration
AIO_USERNAME = os.getenv('AIO_USERNAME', 'babbiramithun')
AIO_KEY = os.getenv('AIO_KEY', 'YOUR_AIO_KEY_HERE')
AIO_FEED_URL = f"https://io.adafruit.com/api/v2/{AIO_USERNAME}/feeds/command/data"

# Light Commands
LIGHT_CMDS = {
    'neon': {'on': 'SWITCHON3', 'off': 'SWITCHOFF3'},
    'spot': {'on': 'SWITCHON1', 'off': 'SWITCHOFF1'}
}
