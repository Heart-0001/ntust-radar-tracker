# Technology Stack

**Project:** Arduino Nano Dual-Mode IoT System
**Researched:** 2026-05-19
**Confidence note:** WebSearch, Bash, and WebFetch were all denied in this session. All findings
are drawn from training data (knowledge cutoff August 2025) plus direct reading of PROJECT.md.
Confidence levels are set conservatively. Version-pin before coding.

---

## 1. Arduino Libraries

### 1.1 HC-SR04 Ultrasonic Sensor

| Library | Version (last known) | Purpose | Confidence |
|---------|---------------------|---------|------------|
| **NewPing** by Tim Eckhoff | 1.9.7 | HC-SR04 / generic ultrasonic, non-blocking ping, median filter | HIGH |

**Why NewPing:** Uses a timer-interrupt approach instead of `pulseIn()`, which blocks the
CPU for up to 30 ms per reading. On a Nano running a game loop and driving a matrix, that
blocking time is fatal to responsiveness. NewPing's `ping_median(n)` gives noise-filtered
readings with no blocking. It also handles the 2 cm / 400 cm hardware limits gracefully
(returns 0 on no-echo rather than hanging).

**Do NOT use:** Raw `pulseIn()` in your own code. It blocks, has no timeout safety, and
produces noisy single-sample readings. Fine for a beginner sketch, wrong for a dual-mode
system that must stay responsive.

**Do NOT use:** `HCSR04` by Gamegine — it wraps `pulseIn()` and adds no value.

**Arduino Library Manager name:** `NewPing`

---

### 1.2 MAX7219 8x8 LED Matrix

| Library | Version (last known) | Purpose | Confidence |
|---------|---------------------|---------|------------|
| **MD_MAX72XX** by MajicDesigns | 3.5.1 | Low-level pixel/row/column control of MAX7219 chains | HIGH |
| **MD_Parola** by MajicDesigns | 3.7.3 | Text scrolling / animation layer on top of MD_MAX72XX | HIGH |

**Why MD_MAX72XX + MD_Parola:** This is the de-facto standard pair for MAX7219 on Arduino.
MD_MAX72XX handles the SPI protocol and abstracts the 8x8 pixel grid. MD_Parola adds
ready-made text scroll, wipe, and fade effects used for the monitoring display.
For the game mode you use MD_MAX72XX directly for full pixel control (no text needed,
need raw setPoint / setRow access). Both libraries are actively maintained by the same
author, share the same SPI bus instance, and chain correctly with a single CS pin.

**Do NOT use:** `LedControl` by Eberhard Fahle — older, still functional, but has no
chaining abstraction and its coordinate system is unintuitive. MD_MAX72XX supersedes it.

**Do NOT use:** `Adafruit_GFX` + MAX7219 adapters — overkill RAM cost for a 2 KB SRAM Nano.

**Arduino Library Manager names:** `MD_MAX72XX`, `MD_Parola`

---

### 1.3 I2C LCD

| Library | Version (last known) | Purpose | Confidence |
|---------|---------------------|---------|------------|
| **LiquidCrystal_I2C** by Frank de Brabander | 1.1.2 | I2C PCF8574 backpack + HD44780 LCD | HIGH |

**Why LiquidCrystal_I2C:** Every I2C LCD backpack sold in 2024-2025 uses a PCF8574 I/O
expander. This library is the universal driver for that chip/LCD combination. Its API is
identical to the built-in `LiquidCrystal` (no relearning). It handles the nibble-mode
protocol internally.

**Default I2C address:** 0x27 (PCF8574) or 0x3F (PCF8574A) — check your specific module
with an I2C scanner sketch before coding.

**Do NOT use:** The built-in `LiquidCrystal` — it requires 6 digital pins. You need those
pins. The I2C variant uses only SDA/SCL (A4/A5 on Nano).

**Arduino Library Manager name:** `LiquidCrystal I2C`

---

### 1.4 7-Segment Display

Two hardware variants are common; the stack differs.

#### If using TM1637 module (4-digit, 2-wire CLK/DIO — most common in kits)

| Library | Version (last known) | Purpose | Confidence |
|---------|---------------------|---------|------------|
| **TM1637Display** by Avishay Orpaz | 1.2.0 | TM1637 driver: digits, colon, brightness | HIGH |

**Why TM1637Display:** The TM1637 chip uses a proprietary 2-wire protocol (not I2C, not SPI).
This library is the standard implementation. The API (`showNumberDec`, `showNumberDecEx`) is
minimal and correct. It does NOT require Wire or SPI — uses bit-banged GPIO.

**Do NOT use:** Custom bit-bang code — timing is fiddly and the library handles it correctly.

**Arduino Library Manager name:** `TM1637Display`

#### If using raw common-cathode/anode 7-segment with shift register (74HC595)

No library needed. Drive the 74HC595 with `shiftOut()` (built-in Arduino function) and
a lookup table of segment bit patterns. This uses 3 pins and zero SRAM for library overhead.
Recommended if you have a 74HC595 available, since TM1637 modules consume one more GPIO pair.

---

### 1.5 Numpad (Matrix Keypad)

| Library | Version (last known) | Purpose | Confidence |
|---------|---------------------|---------|------------|
| **Keypad** by Mark Stanley & Alexander Brevig | 3.1.1 | Matrix keypad scanning, debounce, hold/release events | HIGH |

**Why Keypad:** Standard 4x4 or 4x3 matrix keypads require row/column scanning with
debounce. Writing this correctly from scratch is error-prone (ghost key prevention,
debounce timing). This library handles all of it. It supports `getKey()` (non-blocking
poll) and `waitForKey()` — use `getKey()` in your main loop to stay non-blocking.

**Pin cost:** A 4x4 numpad needs 8 GPIO pins. On the Nano (14 digital + 6 analog-as-digital)
with SPI (3 pins) + I2C (2 pins) + TX/RX (2 pins) + buzzer + LEDs + buttons, pin count is
tight. Plan your pin map before writing code (see Architecture notes).

**Alternative if pins are critically short:** I2C numpad module (PCF8574-based) — shares
the I2C bus but adds I2C address management. The standard `Keypad` library has a companion
`Keypad_I2C` fork that supports this.

**Arduino Library Manager name:** `Keypad`

---

### 1.6 IR Sensor

The PROJECT.md specifies an "IR receiver module" (object detection / presence, not remote
control). Two common variants:

**Variant A: Digital IR proximity module (TCRT5000, FC-51, E18-D80NK)**
- No library needed. These output a digital HIGH/LOW on a GPIO pin. Use `digitalRead()`.
- Sensitivity adjusted via onboard potentiometer.

**Variant B: IR remote receiver (VS1838B, TSOP38238) for decoding NEC/RC5 signals**

| Library | Version (last known) | Purpose | Confidence |
|---------|---------------------|---------|------------|
| **IRremote** by Armin Stumpp (formerly Ken Shirriff) | 4.4.0 | Decode IR remote protocols, send IR | HIGH |

**Why IRremote v4.x:** Version 4 was a major refactor. The API changed from v2/v3
(no more `decode_results` struct; use `IrReceiver.decode()` and `IrReceiver.decodedIRData`).
If you find tutorials using the old `results.value` pattern, they target v2/v3 and will
not compile with v4.

**Important:** IRremote uses Timer2 on AVR. MD_MAX72XX uses SPI (hardware). NewPing can
use Timer1 or Timer2 in async mode — configure NewPing to avoid Timer2 conflict:
`#define TIMER_ENABLED false` or use Timer1. Verify timer assignments before combining.

**If the sensor is simply "does object exist" (digital output):** Do not install IRremote
at all — `digitalRead()` is sufficient and saves ~1.5 KB Flash.

**Arduino Library Manager name:** `IRremote`

---

### 1.7 Buzzer

No library needed for a passive buzzer. Use Arduino's built-in `tone(pin, frequency, duration)`.
For an active buzzer (DC-driven), use `digitalWrite()`.

**Do NOT use:** Any "buzzer library" from the Arduino ecosystem — they all wrap `tone()`
with no meaningful abstraction and consume Flash.

---

### 1.8 SRAM Budget Warning (CRITICAL)

The ATmega328P has **2048 bytes of SRAM**. A rough budget for this project:

| Component | Approx. SRAM cost |
|-----------|------------------|
| LiquidCrystal_I2C | ~60 bytes |
| MD_MAX72XX (1 matrix) | ~96 bytes |
| MD_Parola | ~120 bytes |
| Keypad (4x4) | ~80 bytes |
| NewPing | ~20 bytes |
| String/Serial buffers | ~128-256 bytes |
| Stack + locals | ~200-400 bytes |
| **Total estimate** | **~700-1000 bytes overhead** |

This leaves ~1000-1300 bytes for your program variables, game state, and the Serial JSON
buffer. **Do NOT use Arduino `String` class** — heap fragmentation on 2 KB SRAM causes
random crashes after hours of runtime. Use `char[]` arrays and `snprintf()` exclusively.

---

## 2. Serial Communication Protocol

### Recommendation: JSON with fixed schema, 115200 baud

**Format:**

```
Arduino → PC (sensor data, status):
{"t":"sensor","d":150,"ir":1,"mode":"monitor"}\n

Arduino → PC (game event):
{"t":"game","score":42,"state":"over"}\n

PC → Arduino (command):
{"cmd":"mode","val":"game"}\n
{"cmd":"buzz","val":500}\n
{"cmd":"lcd","val":"Hello"}\n
```

**Why JSON over CSV:**

| Criterion | JSON | CSV |
|-----------|------|-----|
| Self-describing | Yes — field names travel with data | No — parser must know column order |
| Adding fields later | No protocol version bump needed | Breaks parsers if order changes |
| Node-RED native parse | `JSON.parse()` node, zero code | String split, brittle |
| Python native parse | `json.loads()`, one line | `split(',')`, fragile |
| Arduino Flash cost | ~200 bytes for `snprintf` JSON builder | ~50 bytes |
| Human-readable on Serial Monitor | Yes | Barely |

**Why not MessagePack / binary:** Requires matching codec on both ends. Debugging with
Serial Monitor becomes impossible. For a 10-Hz sensor stream over USB, bandwidth is not
a constraint — 115200 baud handles ~1400 bytes/frame easily.

**Why 115200 baud over 9600:** At 9600, a 50-byte JSON frame takes ~52 ms to transmit,
limiting you to ~19 Hz maximum throughput before the serial buffer fills. At 115200 it
takes ~4.3 ms, leaving the CPU free. The USB-serial chip on the Nano (CH340 or FTDI)
handles 115200 reliably on all major OSes in 2025.

**Framing rule:** Always terminate with `\n` and parse line-by-line on the PC side.
`Serial.println()` on Arduino, `readline()` semantics on PC side. This prevents partial-
frame reads and makes buffer management trivial.

**Do NOT use:** `Serial.print()` without a newline terminator — PC-side parsers receive
concatenated frames. Do NOT use COBS or SLIP framing — unnecessary complexity for
USB serial at human-readable rates.

---

## 3. Web Stack Recommendation

### Recommendation: Python + FastAPI + pyserial + WebSocket

**Do NOT use Node-RED as the primary stack for this project.**

**Rationale:**

Node-RED is excellent for rapid prototyping of pure data pipelines, but this project
has two requirements that push against it:

1. **Game score / command logic needs real code.** The game mode sends scores to the web
   dashboard and receives mode-switch commands. In Node-RED, any logic beyond simple
   routing requires Function nodes (raw JavaScript in a text box with no IDE support,
   no type checking, no git-friendly diffs). The project's command-response protocol is
   business logic, not wiring.

2. **Custom UI.** Node-RED's Dashboard 2.0 (for Node-RED 3.x) gives you pre-built widgets
   but a constrained layout system. For a project that wants to show sensor gauges, a live
   matrix display representation, and a game score panel simultaneously, a plain HTML/JS
   page with full CSS control is faster to build and maintain.

**Recommended stack:**

| Layer | Technology | Version | Why |
|-------|-----------|---------|-----|
| Serial bridge | **pyserial** | 3.5 | The standard Python serial library; `Serial` with `readline()` gives clean line-framing |
| Web framework | **FastAPI** | 0.111+ | Async-native, built-in WebSocket support, auto-generates OpenAPI docs, minimal boilerplate |
| ASGI server | **uvicorn** | 0.29+ | Pairs with FastAPI; single command to run; handles concurrent WebSocket + HTTP |
| Frontend | **Vanilla HTML + JS** | — | No build step, no bundler, works in any browser; Chart.js for gauges |
| Charting | **Chart.js** | 4.x | CDN-loaded, real-time streaming chart support via `update()`, zero build step |
| Background serial reader | **asyncio + run_in_executor** | stdlib | Bridges blocking pyserial with async FastAPI without threads crashing each other |

**Why FastAPI over Flask:**

Flask is synchronous. Serial reading (blocking `readline()`) in a Flask app requires
a background thread and a thread-safe queue to push data to WebSocket clients. FastAPI's
async model handles this more cleanly: run the serial reader in a thread pool via
`asyncio.run_in_executor`, broadcast to WebSocket clients from the async event loop.
The result is ~50 fewer lines of synchronization code and no risk of race conditions on
the shared data buffer.

**Why NOT Flask:**
- Synchronous request handling means you need `threading` + `queue` to avoid blocking
  the web server while waiting for serial data.
- Flask-SocketIO adds another dependency and its own event loop that conflicts with
  uvicorn if you later switch frameworks.

**Why NOT Node-RED (expanded):**
- `node-red-node-serialport` requires matching Node.js serialport bindings compiled for
  your OS/Node version — a frequent source of install failures on Windows (node-gyp build
  errors, Python 2 dependency historically required for node-gyp).
- Dashboard 2.0 is a separate npm package with its own versioning; breaking changes
  between Node-RED 3.x and Dashboard 2.0 releases have caused community frustration.
- Debugging flows requires the Node-RED browser UI — no CLI, no standard test framework.
- **If the requirement were "monitor only, no game commands, no custom UI":** Node-RED
  would be the right choice. For this project's scope, Python is cleaner.

**Python version:** 3.11+ (available on all platforms; `asyncio` improvements since 3.10
make the serial bridge pattern cleaner).

---

## 4. Real-Time Data: Browser Communication

### Recommendation: WebSocket (not SSE, not polling)

| Method | Bidirectional | Latency | Browser support | Complexity |
|--------|--------------|---------|----------------|------------|
| **WebSocket** | Yes | ~1-5 ms | Universal | Low-medium |
| SSE (Server-Sent Events) | No (server → client only) | ~5-20 ms | Universal | Low |
| HTTP Polling | No | 0.5-2 s typical | Universal | Very low |

**Why WebSocket:** The project requires bidirectional communication — sensor data flows
browser-ward, control commands flow Arduino-ward. SSE is one-directional (server push
only); you would need a separate POST endpoint for commands, creating two separate
connection management problems. Polling introduces 500 ms-2 s latency and hammers the
server with unnecessary requests.

WebSocket with FastAPI:

```python
# Minimal pattern — FastAPI WebSocket broadcast
from fastapi import FastAPI, WebSocket
import asyncio, json

app = FastAPI()
clients: list[WebSocket] = []

@app.websocket("/ws")
async def ws_endpoint(websocket: WebSocket):
    await websocket.accept()
    clients.append(websocket)
    try:
        while True:
            data = await websocket.receive_text()
            # data is a command from browser → forward to serial
            await serial_queue.put(json.loads(data))
    except:
        clients.remove(websocket)

async def serial_broadcast(message: str):
    for client in clients[:]:
        try:
            await client.send_text(message)
        except:
            clients.remove(client)
```

Browser-side (vanilla JS, no libraries):

```javascript
const ws = new WebSocket("ws://localhost:8000/ws");
ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    updateChart(data.d);        // sensor reading
    updateMatrix(data.mode);    // mode indicator
};

function sendCommand(cmd, val) {
    ws.send(JSON.stringify({ cmd, val }));
}
```

**Chart.js streaming pattern:**

Use `chart.data.datasets[0].data.push(value)` and `chart.update('none')` (the `'none'`
argument disables animation on each update, essential for smooth real-time rendering).
Limit the data array to the last N points by shifting old values off: `data.shift()`.

**Do NOT use:** Socket.IO — it adds a 45 KB client bundle for a protocol abstraction you
do not need. Native WebSocket works in every browser since 2012.

**Do NOT use:** MQTT broker (Mosquitto) + websocket bridge — unnecessary infrastructure
for a USB-serial-to-browser pipeline. MQTT is right when multiple IoT devices publish
to a cloud broker; for one device to one screen, it is over-engineering.

---

## 5. Recommended Stack Summary

### Full Stack at a Glance

```
[Arduino Nano ATmega328P]
  Libraries:
    NewPing 1.9.7          — HC-SR04 non-blocking
    MD_MAX72XX 3.5.1       — MAX7219 pixel control
    MD_Parola 3.7.3        — text scroll (monitoring mode)
    LiquidCrystal_I2C 1.1.2 — I2C LCD
    TM1637Display 1.2.0    — 7-segment (if TM1637 module)
    Keypad 3.1.1           — 4x4 numpad matrix scan
    IRremote 4.4.0         — IR decode (only if RC5/NEC remote needed)
  Serial: JSON over UART, 115200 baud, \n-terminated
      |
      | USB Serial (CH340/FTDI)
      |
[Python 3.11+ host process]
  pyserial 3.5            — serial readline thread
  FastAPI 0.111+          — HTTP + WebSocket server
  uvicorn 0.29+           — ASGI runner
      |
      | WebSocket ws://localhost:8000/ws
      |
[Browser]
  Vanilla HTML + JS       — dashboard UI
  Chart.js 4.x (CDN)     — real-time sensor chart
```

### Install Commands

```bash
# Python dependencies
pip install fastapi uvicorn pyserial

# Arduino Library Manager (Arduino IDE or CLI)
# Search and install each by name:
#   NewPing
#   MD_MAX72XX
#   MD_Parola
#   LiquidCrystal I2C
#   TM1637Display
#   Keypad
#   IRremote  (only if needed)
```

### What NOT to Use — Decision Table

| Technology | Why Excluded |
|-----------|-------------|
| Node-RED | Logic beyond routing requires JavaScript in text boxes; Dashboard 2.0 layout is constrained; Windows serialport install is fragile |
| Flask | Synchronous; requires manual threading + queues for serial bridge; more boilerplate than FastAPI |
| Socket.IO | Adds 45 KB browser bundle for no capability gain over native WebSocket |
| MQTT + Mosquitto | Over-engineering for single-device local USB connection |
| LedControl | Superseded by MD_MAX72XX; no chain abstraction |
| Arduino `String` class | Heap fragmentation on 2 KB SRAM causes runtime crashes |
| `pulseIn()` for HC-SR04 | Blocks CPU for up to 30 ms; breaks game loop responsiveness |
| MessagePack / binary serial | Debugging with Serial Monitor becomes impossible |
| 9600 baud | Limits throughput to ~19 Hz for 50-byte frames; too slow |
| SSE for dashboard | One-directional; cannot send commands back to Arduino |

---

## 6. Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Arduino library selection | HIGH | All listed libraries are well-established, no network verification possible but training data current to Aug 2025 |
| Library versions | MEDIUM | Version numbers reflect last known state; **verify in Arduino Library Manager before pinning** |
| Serial JSON protocol | HIGH | Rationale is based on protocol properties, not version-sensitive |
| FastAPI + WebSocket | HIGH | FastAPI 0.100+ has stable WebSocket API; asyncio serial bridge is a well-documented pattern |
| Node-RED rejection rationale | HIGH | Based on documented architecture constraints and known Windows serialport issues |
| Chart.js real-time pattern | HIGH | `update('none')` + `shift()` is the documented streaming pattern for Chart.js 4.x |

---

## 7. Open Questions / Verify Before Coding

1. **IR sensor type** — Is it a digital proximity module (no library needed) or an IR remote
   receiver needing protocol decode? Confirm with hardware in hand before installing IRremote.

2. **7-segment variant** — TM1637 module or bare 7-segment + 74HC595? Pin count budget
   changes significantly between the two.

3. **Numpad connection** — Standard matrix (8 GPIO) or I2C PCF8574 version?
   If matrix, you need to verify the pin map against SPI (D10/D11/D13) and I2C (A4/A5).

4. **Timer conflicts** — NewPing async mode and IRremote both use AVR timers. If both
   libraries are active simultaneously, verify which timer each claims via their source
   headers (`#define USE_TIMER_1/2`) before combining.

5. **I2C address conflicts** — LCD at 0x27 or 0x3F. If you add an I2C numpad expander
   (PCF8574), its default address is also 0x20-0x27. Run the I2C scanner sketch first.

6. **Python serial port name** — Windows: `COM3`, `COM4`, etc. The FastAPI app needs a
   config constant or environment variable for this — hardcoding `COM3` will break on
   other machines. Use a `.env` file or CLI argument.

---

*Sources: Arduino official library documentation (training data, cutoff Aug 2025),
FastAPI official docs (training data), pyserial documentation (training data),
MD_MAX72XX/MD_Parola GitHub (MajicDesigns, training data). No live network
verification was possible in this session — all versions should be confirmed
against Arduino Library Manager and PyPI before use.*
