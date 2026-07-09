# Pi ↔ Master ESP32 ↔ Slave: Communication Only

This document describes **only how communication works** between the Raspberry Pi, the Master ESP32, and the slaves. It does not describe what the slaves do (sensors, LEDs, etc.). Use it so another LLM or project can implement the same Pi–Master–Slave link and protocol.

---

## 1. Architecture (Communication Paths)

```
                    Serial (UART) 115200 baud              ESP-NOW (wireless)
   ┌─────────────┐   ────────────────────────────────   ┌─────────────┐   ───────────────────   ┌─────────────┐
   │ Raspberry   │   TX/RX lines, one line per message    │   Master    │   binary frames by MAC  │   Slave(s)   │
   │ Pi          │ ◄──────────────────────────────────► │   ESP32     │ ◄─────────────────────► │   ESP32     │
   └─────────────┘   /dev/serial0 or /dev/ttyUSB0        └─────────────┘   (no interpretation)   └─────────────┘
```

- **Pi ↔ Master**: Single serial link. **115200 baud.** Text, line-based: one message = one line ending with newline.
- **Master ↔ Slaves**: **ESP-NOW**. Master does **not** interpret payloads; it only forwards by **MAC address**. Each slave has a 6-byte MAC (e.g. `A0:A3:B3:2A:20:C0`).

Slaves are treated as endpoints identified by MAC; what they do with the bytes is outside this document.

### 1.1 Generic design: who hardcodes which MAC (reusable in any project)

- **Master is generic.** The Master firmware must **not** hardcode any slave MAC. It learns slave addresses only when the Pi sends `TX:<TARGET_MAC>:<HEX>`. The Master adds that MAC as an ESP-NOW peer on first use and then forwards the payload. So you can flash the **same** Master firmware in any project; it never needs to know your slaves in advance.
- **Only the slave hardcodes the Master’s MAC.** Each slave must know the Master’s MAC so it can:
  - Add the Master as an ESP-NOW peer in `setup()`,
  - Send data to the Master with `esp_now_send(masterMAC, ...)`.
  So in the **slave** firmware you set e.g. `uint8_t masterMAC[] = {0xF0, 0x24, 0xF9, 0x0D, 0x90, 0xA4};` (replace with your Master’s actual MAC). The Pi does not need the Master’s MAC for the protocol: the Pi only sends `TX:<slave_MAC>:...`; the Master just forwards.
- **Pi knows slave MACs from its own config.** The Pi needs a list of slave MACs (or friendly names → MAC) only so it can send commands to the right slave. That list lives in Pi config (e.g. `config.py` or `config.json`), not in the Master.

So for any new project: use the **same** generic Master; in each **slave** set the Master’s MAC; on the **Pi** set the serial port and the list of slave MACs. No changes to the Master.

---

## 2. How the Master Is Connected and How Reconnection Works

This section is for another LLM: it describes the **connection model** (single serial link, who owns it, when it is opened/closed) and the **reconnection algorithm** (how the Pi recovers when the link drops or the Master stops responding).

### 2.1 Connection model (Pi side)

- **Single serial connection:** The Pi has exactly one serial port open to the Master at a time. One process (e.g. the controller) owns that port; no other process should open it.
- **Baud rate:** 115200. **Timeout:** 1 s on read (so `readline()` returns when a line is complete or after 1 s).
- **After opening the port:** The Pi toggles **DTR** (DTR = False → sleep 0.1 s → DTR = True). This resets the Master ESP32 so it boots into a known state. If you use GPIO UART (no USB), DTR may not be available; then the Master is not reset by the Pi and may need a power cycle.
- **Threads:**  
  - **Reader thread:** Only this thread reads from the serial port. It runs in a loop: read lines, parse them, and optionally “pet” a watchdog when any line is received.  
  - **Watchdog thread:** Runs separately. It checks that the Pi has received *some* line from the Master within the last N seconds (e.g. 60 s). If not, it assumes the Master is stuck and triggers a **Master reset** via DTR (see below).  
- **Sending:** Any other thread/code that wants to send to the Master must call a single “send” function that writes the line to the same serial port. If the port is closed or write fails, that function closes the port (so the reader thread will reconnect) and does not retry the same command automatically.

So: **one port, one reader thread, one watchdog, DTR used for reset after open and when watchdog fires.**

### 2.2 Reconnection algorithm (step-by-step for another LLM)

Goal: If the serial link is lost (unplug, crash, Master hang) or the Master stops sending anything, the Pi should close the port, reopen it when possible, and optionally reset the Master so the link comes back without restarting the Pi process.

**State the Pi maintains:**

- `serial_conn` — the open serial port object, or `None` if closed.
- `running` — boolean; while true, the reader loop and watchdog keep running.
- Watchdog: `last_pet` — timestamp of the last time the Pi received *any* line from the Master. “Pet” = set `last_pet = now`.

**Startup (when the controller starts):**

1. Call **connect()**: open serial port at 115200, timeout 1 s. Then DTR = False, sleep 0.1 s, DTR = True. If open fails, abort (no reader/watchdog started).
2. Set `running = True`.
3. Start the **watchdog thread**, passing the current `serial_conn`. Watchdog holds a reference to `serial_conn` so it can use it for DTR reset; you will update this reference after reconnect.
4. Start the **reader thread**.

**Reader thread loop (runs until `running` is false):**

1. **If `serial_conn` is None or not open:**  
   - Call **reconnect()** (see below).  
   - If after reconnect `serial_conn` is still None, sleep 5 s, then go to top of loop (retry later).  
   - Otherwise continue to step 2.
2. **If there is data to read (`in_waiting > 0`):**  
   - Read one line (e.g. `readline()`), decode to string, strip.  
   - If the line is non-empty: append to your log if needed, **pet the watchdog** (set `last_pet = now`), then parse and handle the line (RX:/OK:/ERR:/HEARTBEAT).  
   - Then go to top of loop.
3. **If there is no data:**  
   - Sleep a short time (e.g. 0.02 s) to avoid busy-loop and high CPU. Then go to top of loop.
4. **On exception:**  
   - If the exception is **SerialException, OSError, or IOError:** log “Serial error (will reconnect)”, call **close_serial()** (see below), sleep 2 s, then go to top of loop.  
   - For any other exception: log, sleep 1 s, go to top of loop.

**close_serial():**

- If `serial_conn` exists and is open, close it. Set `serial_conn = None`. If a watchdog object exists, set `watchdog.serial_conn = None` so the watchdog does not use a stale handle.

**reconnect():**

1. Call **close_serial()**.
2. Try: open serial port (same port name, 115200, timeout 1). Then DTR = False, sleep 0.1 s, DTR = True. If a watchdog exists, set `watchdog.serial_conn = self.serial_conn` so the watchdog uses the new port. Return success.
3. On failure (e.g. SerialException): log, leave `serial_conn` as None, return failure.

**Watchdog thread loop:**

- Every 1 s: if `running` is false, exit.  
- If `(now - last_pet) > timeout` (e.g. 60 s): log “WATCHDOG TRIGGERED”, call **reset_master()**, then **pet** the watchdog (set `last_pet = now`) so you don’t trigger again immediately while the Master reboots.  
- Sleep 1 s, repeat.

**reset_master():**

- If `serial_conn` is None or not open: log “Cannot reset master” and return.  
- Otherwise: set DTR = False, sleep 0.1 s, DTR = True, sleep 0.1 s, DTR = False. This resets the Master ESP32 (if connected via USB; GPIO UART may not support DTR). Log “Master Reset Signal Sent”.

**When sending a command (e.g. TX:...):**

- If `serial_conn` is None or not open: log “Serial connection lost. Cannot send.” and do not write.  
- Otherwise: write the line (e.g. `TX:<MAC>:<HEX>\n`).  
- On **SerialException or OSError** on write: log, then call **close_serial()**. The reader thread will see that the port is closed and will call **reconnect()** on the next iteration.

**Summary for another LLM:**

- **One** serial connection; **one** thread reads; **one** thread watches “last received line” and resets the Master via DTR if no line for 60 s.  
- **Reconnect:** Reader thread detects “no port or not open” or a read exception → close port → reopen (with DTR toggle) in a loop, with 5 s sleep when reopen fails and 2 s sleep after a read exception.  
- **Send path:** If write fails, close the port; reconnection is handled by the reader thread.  
- **Master reset:** Watchdog triggers DTR toggle; reconnect uses DTR toggle after open. Both help the Master come back to a clean state.

---

## 3. Serial Protocol (Pi ↔ Master)

Everything on the serial link is **ASCII lines** (UTF-8), **115200 baud**, **newline-terminated** (`\n`). No binary on the wire between Pi and Master.

### 3.1 Pi → Master (sending to a slave)

The Pi sends **exactly one line** per command:

```
TX:<TARGET_MAC>:<HEX_PAYLOAD>
```

- **TX:**  Literal prefix (capital letters).
- **TARGET_MAC:**  Slave’s MAC in form `XX:XX:XX:XX:XX:XX` (e.g. `A0:A3:B3:2A:20:C0`). Colons required.
- **HEX_PAYLOAD:**  Even number of hex digits (0–9, A–F, a–f). No spaces. This is the raw bytes to send to that slave via ESP-NOW (Master sends these bytes as-is).

**Example:**  
`TX:A0:A3:B3:2A:20:C0:02100000803F`  
→ Master will send the 6 bytes `02 10 00 00 80 3F` to the slave with MAC `A0:A3:B3:2A:20:C0`.

**Rule for parsing on Master:**  First `:` after `TX`; then the **last** `:` separates MAC from payload. So the payload is “everything after the last colon” and can contain no colons (recommended) or you define that the payload is the rest of the line after the second colon; this repo uses “substring after last colon” for the hex.

### 3.2 Master → Pi (what the Pi receives)

The Pi reads **lines**. Each line is one of:

| Line format        | Meaning |
|--------------------|--------|
| `RX:<SENDER_MAC>:<HEX_PAYLOAD>` | A slave with **SENDER_MAC** sent bytes to the Master; Master forwards them as hex. **SENDER_MAC** is who sent it; **HEX_PAYLOAD** is the raw bytes in hex (even length). |
| `OK:Sent`          | The last `TX:...` command was sent over ESP-NOW successfully. |
| `ERR:...`          | Something failed (e.g. `ERR:Send Failed`, `ERR:Format`, `ERR:PeerAdd`). |
| `HEARTBEAT`        | Keepalive from Master every ~10 s; use it to detect that the serial link and Master are alive. |

**Example:**  
`RX:F0:24:F9:0C:DE:54:0160000000E040`  
→ Slave `F0:24:F9:0C:DE:54` sent 6 bytes (hex `01 60 00 00 00 E0 40`). The Pi can parse and use that; the Master does not care about the meaning.

**Important:**  The Master is **transparent**. It does not interpret `HEX_PAYLOAD`; it only forwards bytes to/from slaves by MAC. All meaning of the payload is defined by your Pi code and your slave firmware.

---

## 4. How the Master ESP32 Behaves (So You Don’t Have to Change It)

- **Serial:**  `Serial.begin(115200)`. Reads lines from the Pi; sends lines to the Pi.
- **ESP-NOW:**  `WiFi.mode(WIFI_STA)`, `esp_now_init()`, recv/send callbacks registered.
- **On line from Pi starting with `TX:`:**  
  - Parse `TARGET_MAC` and `HEX_PAYLOAD` (payload = after last `:`).  
  - If that MAC is not yet a peer, add it (channel 0, no encryption).  
  - Convert hex string to bytes and call `esp_now_send(peer_addr, buffer, len)`.  
  - Then print `OK:Sent` or `ERR:Send Failed` (and similar for format/peer errors).
- **On ESP-NOW receive from any slave:**  
  - Print one line: `RX:<SENDER_MAC>:<HEX>`, where SENDER_MAC is the slave’s MAC and HEX is the received payload in hex.
- **Every ~10 s:**  Print `HEARTBEAT`.

So for **any** other project: keep this Master firmware as-is. You only change what the **Pi** does with `RX:...` lines and what bytes it sends in `TX:...`, and what the **slaves** do with the bytes they receive.

---

## 5. What the Pi Must Do (For Another LLM Implementing This)

To use this communication in another project, the Pi (or any “controller” talking to the Master over serial) must:

1. **Open the serial port** at **115200** baud (e.g. `/dev/serial0` for GPIO or `/dev/ttyUSB0`/`/dev/ttyACM0` for USB).
2. **Send commands to a slave** by writing exactly one line:  
   `TX:<MAC>:<HEX>\n`  
   where MAC is `XX:XX:XX:XX:XX:XX` and HEX is the raw payload in hex (even length). No spaces in HEX.
3. **Read lines** from the Master. For each line:
   - If it starts with **`RX:`** → a slave sent data. Parse MAC and hex payload and handle in your application (this doc does not define payload meaning).
   - If it is **`OK:Sent`** → last TX was delivered by the Master to ESP-NOW.
   - If it starts with **`ERR:`** → last TX failed or invalid format; handle as error.
   - If it is **`HEARTBEAT`** → link alive; optional: use for watchdog (e.g. reset Master if no line for 60 s).
4. **Optional but recommended:**  After opening the port, toggle **DTR** (low → short delay → high) to reset the Master so it starts clean. If using GPIO UART (no USB), DTR may not exist; then rely on power cycle.
5. **Optional:**  On serial read/write errors, close and reopen the port (reconnect logic).

That is the full contract for **communication** between Pi and Master. No slave application logic is required to understand this.

---

## 6. Pi Hardware Setup (So the Serial Port Works)

The Pi talks to the Master over **UART**. Either **GPIO** or **USB**.

### 6.1 Using GPIO UART (`/dev/serial0`)

- **Enable UART:**  In `/boot/firmware/config.txt` (or `/boot/config.txt`):  
  `enable_uart=1`
- **Disable serial console:**  So the kernel does not use the port for login.  
  - `sudo raspi-config` → Interface Options → Serial Port → **No** to “Login shell over serial”, **Yes** to “Serial port enabled”.  
  - Or remove `console=serial0,115200` from `cmdline.txt`.
- **If the port is missing or flaky (Pi 3/4/5):**  The UART can be shared with Bluetooth. To free it, add in `config.txt`:  
  `dtoverlay=disable-bt`  
  Then reboot. The UART will be on GPIO 14/15; Pi Bluetooth will be off.
- **Wiring (3.3 V only, common GND):**

  | Pi              | Master ESP32   |
  |-----------------|----------------|
  | GPIO 14 (TX) Pin 8  | GPIO 3 (RX)  |
  | GPIO 15 (RX) Pin 10 | GPIO 1 (TX)  |
  | GND (e.g. Pin 6)    | GND          |

  So: **Pi TX → ESP32 RX**, **Pi RX → ESP32 TX**, **GND ↔ GND**.

### 6.2 Using USB

- Connect the Master ESP32 to the Pi with a USB cable. The device appears as `/dev/ttyUSB0` or `/dev/ttyACM0`.
- In your Pi code, set the serial port to that device. No UART/Bluetooth config needed for the link; only permissions (below).

### 6.3 Permissions

- Add your user to `dialout`:  
  `sudo usermod -aG dialout $USER`  
  Then log out and back in (or reboot).
- If connection fails, check that no other process has the port:  
  `lsof /dev/serial0` or `lsof /dev/ttyUSB0`.

### 6.4 Reboot

After any change to `config.txt` or `cmdline.txt`, run:  
`sudo reboot`

---

## 7. Comparison: house_automation vs ESP_now_uart (communication only)

Both use **Pi ← serial → Master ← ESP-NOW → Slaves**. The following compares only the **communication** part so you can reuse the same generic design in another project.

| Aspect | house_automation | ESP_now_uart (common_esp_now_framework) |
|--------|------------------|----------------------------------------|
| **Master hardcodes slave MACs?** | **No.** Peers added when Pi sends `TX:<MAC>:...`. Master is generic. | **No.** Same: peers added on first TX. Master is generic. |
| **Slave hardcodes Master MAC?** | **Yes.** e.g. `SlaveComms.h` / `Config.h`: `MASTER_MAC[]`. | **Yes.** e.g. `slave_template.ino`: `masterMAC[]`. |
| **Pi → Master serial** | `Serial` (USB or GPIO1/3). One build works for USB or GPIO wiring. | `HardwareSerial(1)` on **GPIO 16 (RX), 17 (TX)**. Pi must be wired to 16/17; USB `Serial` is debug only. |
| **Pi port** | `/dev/serial0` (GPIO) or `/dev/ttyUSB0` (USB). | Default `/dev/ttyUSB0` (README suggests USB); for GPIO use `/dev/ttyS0` and wire Pi 14/15 ↔ ESP32 16/17. |
| **Protocol: Pi → Master** | `TX:<MAC>:<HEX>\n` | Same. Also supports `PING` → Master replies with HEARTBEAT + MAC. |
| **Protocol: Master → Pi** | `RX:<MAC>:<HEX>`, `OK:Sent`, `ERR:...`, `HEARTBEAT` (every 10 s). | Same idea; plus `MAC:<MASTER_MAC>` (so Pi can discover Master MAC), `MASTER_READY` on boot. `OK`/`ERR:Send` (no “:Sent”). HEARTBEAT every 2 s. |
| **Reconnection on Pi** | **Full:** reader thread closes port on error, calls reconnect() in a loop (sleep 5 s if open fails, 2 s after read error). Watchdog resets Master via DTR. | **None:** reader runs only while port is open; no reconnect. If port drops, process must restart. |
| **DTR on connect** | Yes: toggle DTR after open to reset Master. | Not in controller (no DTR in connect); watchdog does DTR reset. |

**Takeaways for a generic setup:**

- **Master stays generic in both:** no slave MACs in Master; only slaves hardcode the Master’s MAC. You didn’t miss anything for that.
- **house_automation** is stronger on **reconnection** (auto-reconnect + DTR on open). Use that pattern if you want the Pi to recover from unplug/crash without restarting the app.
- **ESP_now_uart** Master sends **MAC:** and **MASTER_READY** so the Pi can learn the Master’s MAC and know when the Master is up. Optional; add to house_automation Master if you want that.
- **UART pins:** house_automation Master uses `Serial` (USB/GPIO1,3); ESP_now_uart Master uses a second UART on 16/17. For a new project, choose one: same Master firmware everywhere, and wire Pi to the UART that Master uses for Pi.

---

## 8. Protocol Summary (Copy-Paste for Another LLM)

- **Link:**  Serial between Pi and Master. **115200 baud.** Line-based ASCII; one message per line (`\n`).
- **Pi → Master:**  
  `TX:<TARGET_MAC>:<HEX_PAYLOAD>\n`  
  MAC = `XX:XX:XX:XX:XX:XX`, HEX = even number of hex digits (payload bytes in hex).
- **Master → Pi:**  
  - `RX:<SENDER_MAC>:<HEX_PAYLOAD>` — data from slave SENDER_MAC.  
  - `OK:Sent` — last TX sent.  
  - `ERR:...` — error.  
  - `HEARTBEAT` — keepalive ~every 10 s.
- **Master:**  Transparent bridge. Forwards bytes to/from slaves by MAC over ESP-NOW. Does not interpret payloads.
- **Slaves:**  Identified by MAC; they receive/send bytes via ESP-NOW. Meaning of bytes is defined by your Pi and slave code, not by this communication doc.

Use this for any project that needs Pi–Master–Slave communication: same protocol, same Master firmware; only Pi and slave application logic change. **Master is generic (no slave MACs); only each slave hardcodes the Master’s MAC.**
