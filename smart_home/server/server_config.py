# Smart Home Server Configuration

# --- System ---
SERIAL_BAUD = 115200
CHECK_INTERVAL_SEC = 5  # Server loop interval

# --- Hydration Monitor ---
# Time between alerts if no water is consumed
HYDRATION_CHECK_INTERVAL = 30 * 60  # 30 Minutes

# Sleep Mode (No alerts during these hours)
HYDRATION_SLEEP_START = 23  # 11 PM
HYDRATION_SLEEP_END = 10    # 10 AM

# Drinking Logic
HYDRATION_DRINK_THRESHOLD = 50  # Minimum weight drop (grams) to count as a drink
HYDRATION_REFILL_THRESHOLD = 50 # Minimum weight gain (grams) to count as refill
HYDRATION_GOAL = 2000           # Daily Goal (ml)

# Presence
PRESENCE_TIMEOUT = 300          # Seconds before marking user away
PHONE_MAC = "48:EF:1C:49:6A:E7" # User's device (for Presence check)
