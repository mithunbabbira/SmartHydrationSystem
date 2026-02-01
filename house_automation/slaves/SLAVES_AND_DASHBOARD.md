# House Automation – Slaves & Pi Dashboard Reference

This document lists all ESP-NOW slaves, what data they send/receive, and what can be controlled from the Pi dashboard.

---

## 1. Hydration Monitor

**Slave:** `hydration`  
**MAC:** `F0:24:F9:0C:DE:54`  
**Type:** 1 (6-byte packets)

### Data from Slave → Pi (fetched / displayed)

| Command | Description | Dashboard Display |
|---------|-------------|-------------------|
| 0x21 REPORT_WEIGHT | Current weight (g) | **Weight** (ml) – main value |
| 0x60 DRINK_DETECTED | Last drink volume (ml) | **Last drink**; triggers drink celebration (display "drank X.X ml", green LED, green IR for 3s, then revert) |
| 0x61 DAILY_TOTAL | Today's total (ml) | **Today total** |
| 0x30 REQUEST_TIME | Slave asks for time | Pi sends 0x31 with unix timestamp |
| 0x40 REQUEST_PRESENCE | Slave asks if user is home | Pi checks phone, sends 0x41 |
| 0x50 ALERT_MISSING | Bottle missing (timer expired) | Triggers IR + LED alert |
| 0x51 ALERT_REPLACED | Bottle replaced | Reverts IR + LED |
| 0x52 ALERT_REMINDER | Hydration reminder (user home, no drink) | Triggers IR + LED alert |
| 0x53 ALERT_STOPPED | Alert stopped | Reverts IR + LED |

### Commands from Pi → Slave (controlled from dashboard)

| Command | Hex | Description | Dashboard / API |
|---------|-----|-------------|-----------------|
| Tare | 0x22 | Zero scale | **Tare** button |
| LED On | 0x10 (1.0) | Turn hydration LED on | Advanced → **Hydration LED ON** |
| LED Off | 0x10 (0.0) | Turn hydration LED off | Advanced → **Hydration LED OFF** |
| Buzzer On | 0x11 (1.0) | Turn buzzer on | Advanced → **Buzzer ON** |
| Buzzer Off | 0x11 (0.0) | Turn buzzer off | Advanced → **Buzzer OFF** |
| Get Weight | 0x20 | Request current weight | Generic cmd |
| Request Daily Total | 0x23 | Request today total (reply 0x61) | On load + every 60s |
| Sync Time | 0x30 | Slave replies 0x30; Pi sends 0x31 | Advanced → **Sync Time** |
| Ping Presence | 0x40 | Slave replies 0x40; Pi sends 0x41 | Advanced → **Ping Presence** |
| Force Reminder | 0x52 | Simulate alert reminder | Advanced → **Force Reminder** |
| Silence Alert | 0x53 | Simulate alert stopped | Advanced → **Silence Alert** |

### Dashboard UI

- **Main:** Weight, Last drink, Today total  
- **Buttons:** Tare, Signal Alert  
- **Advanced (expand):** Force Reminder, Silence Alert, Hydration LED, Buzzer, Ping Presence, Sync Time  

### API

- `POST /api/hydration/cmd`  
  Body: `{ "cmd": "tare" | "led_on" | "led_off" | "buzzer_on" | "buzzer_off" | "sync_time" | "ping_presence" | "request_daily_total" | "test_alert" | "test_stop" }`
- `GET /api/data` → `hydration: { weight, status, last_update, last_drink_ml, last_drink_time, daily_total_ml }`

---

## 2. LED Ambience (BLE)

**Slave:** `led_ble` (or combo_remote for LED)  
**MAC:** `A0:A3:B3:2A:20:C0`  
**Type:** 2

### Data from Slave → Pi

- None (LED strip does not send data back)

### Commands from Pi → Slave (controlled from dashboard)

| Command | Hex | Description | Dashboard |
|---------|-----|-------------|-----------|
| On | 02 10 00 00 80 3F | Power on | **ON** button |
| Off | 02 10 00 00 00 00 | Power off | **OFF** button |
| RGB | 02 12 &lt;float&gt; | Color ID 1–8 | **Color** buttons (Red, Green, Blue, White, Orange, Purple) |
| Mode | 02 13 &lt;mode&gt;&lt;speed&gt; | Effect + speed | **Effects** (Rainbow 37, Red/Green/Blue/Yellow/Cyan/Purple/White Pulse 38–44, Cross 45–47, Strobe 48–49) |
| Raw | &lt;hex&gt; | Raw BLE payload | **Raw LED** input |

### Dashboard UI

- **Power:** ON, OFF  
- **Colors:** Red, Green, Blue, White, Orange, Purple  
- **Speed:** Slider 1–100  
- **Effects:** Rainbow, Red/Green/Blue/Yellow/Cyan/Purple/White Pulse, R/G Cross, R/B Cross, G/B Cross, Seven Strobe, Red Strobe  
- **Raw:** Hex payload input  

### API

- `POST /api/led/cmd`  
  Body: `{ "cmd": "on" | "off" | "rgb" | "mode" | "effect", "val": <id>, "mode": <id>, "speed": <1-100> }`  
- `POST /api/led/raw`  
  Body: `{ "hex": "<hex>" }`

---

## 3. IR Remote

**Slave:** `ir_remote` (combo_remote)  
**MAC:** `A0:A3:B3:2A:20:C0`  
**Type:** 3

### Data from Slave → Pi

- None (IR remote only sends)

### Commands from Pi → Slave (controlled from dashboard)

| Command | Hex | Description | Dashboard |
|---------|-----|-------------|-----------|
| Send NEC | 03 31 &lt;32-bit hex&gt; | Send NEC IR code | All IR buttons + **Raw IR** |

### Dashboard UI

- **Power:** ON (F7C03F), OFF (F740BF)  
- **Brightness:** Bright+ (F700FF), Bright− (F7807F)  
- **Colors:** Red, Green, Blue, Yellow  
- **Modes:** Smooth (F7F00F), Flash (F7D02F), Fade (F7C03F), Strobe (F7E01F)  
- **Volume:** VOL+, VOL−  
- **Channel:** CH+, CH−  
- **Raw:** Hex input (e.g. F7C03F)  

### API

- `POST /api/ir/send`  
  Body: `{ "code": "<hex>" }` (e.g. `"F7C03F"`)

---

## 4. ONO Display & CAM Display

**Slaves:** `ono_display`, `cam_display`  
**MACs:** `C0:CD:D6:85:70:CC`, `24:DC:C3:AC:B4:14`  
**Type:** 3

### Data from Slave → Pi

- None (displays only receive)

### Commands from Pi → Slave (controlled from dashboard)

| Command | Hex | Description | Dashboard |
|---------|-----|-------------|-----------|
| Rainbow | 03 50 &lt;duration float&gt; | Rainbow effect (sec) | **Rainbow** + duration |
| Color | 03 51 R G B &lt;duration float&gt; | Custom RGB (sec) | **Set Color** + color picker + duration |
| Text | 03 60 &lt;duration&gt; &lt;len&gt; &lt;text&gt; | Show text (scrolls if long) | **Send Text** + duration |
| Price | 03 70 &lt;price float&gt; &lt;change float&gt; | ONO price + 24h change | Auto from Pi (CoinGecko) |

Pi sends price every 15s (fetch from CoinGecko every 60s). If no packet for 90s, displays show "PI down" + rainbow.

### Dashboard UI

- **Text:** Input + duration (sec) + **Send Text**  
- **RGB:** Color picker + duration + **Set Color**  
- **Rainbow:** Duration + **Rainbow** button  

### API

- `POST /api/ono/text`  
  Body: `{ "text": "<string>", "duration": <sec> }`  
- `POST /api/ono/rainbow`  
  Body: `{ "duration": <sec> }`  
- `POST /api/ono/color`  
  Body: `{ "r", "g", "b", "duration" }`

---

## 5. Room Lights (Adafruit IO)

**Not a slave.** Cloud-controlled via Adafruit IO.

### Dashboard UI

- **Neon:** ON, OFF  
- **Spot:** ON, OFF  

### API

- `POST /api/aio/cmd`  
  Body: `{ "device": "neon" | "spot", "action": "on" | "off" }`

---

## 6. Master Control

**Not a slave.** Orchestrates multiple devices.

### Dashboard UI

- **Master ON:** Neon + Spot ON, IR ON, LED ON  
- **Master OFF:** Neon + Spot OFF, IR OFF, LED OFF  

### API

- `POST /api/master/cmd`  
  Body: `{ "action": "on" | "off" }`

---

## 7. Serial Log & Health

### API

- `GET /api/master/log?limit=200` – Serial log from Master ESP32  
- `GET /api/data` – Hydration data (weight, last drink, today total)  
- `GET /api/health?system=true` – Controller + Pi system status  

---

## Summary Table

| Slave      | Fetched Data                    | Sent / Controlled                     | Dashboard Section |
|------------|----------------------------------|---------------------------------------|-------------------|
| Hydration  | Weight, Last drink, Today total  | Tare, LED, Buzzer, Sync time, Presence, Alerts | Hydration Monitor |
| LED BLE    | —                                | On/Off, Colors, Effects, Raw          | LED Ambience      |
| IR Remote  | —                                | NEC codes (ON, OFF, modes, Raw)       | IR Remote         |
| ONO/CAM    | —                                | Text, Rainbow, Color, Price (auto)    | ONO Display       |
| Room Lights| —                                | Neon, Spot (AIO)                      | Room Lights       |
| Master     | —                                | All ON / All OFF                      | Header            |
