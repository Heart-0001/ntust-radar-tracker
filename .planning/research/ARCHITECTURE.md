# Architecture Patterns: Arduino Nano Dual-Mode IoT System

**Project:** Arduino Nano 雙模式 IoT 互動系統
**Researched:** 2026-05-19
**Confidence:** HIGH (established embedded + serial web patterns; no novel technology)

---

## 1. Firmware Architecture

### 1.1 State Machine for Dual-Mode

The firmware top-level is a two-state machine. Every iteration of `loop()` dispatches to the active state handler. Transitions are event-driven (mode button press, or Serial command from host).

```
States: MONITORING | GAME
Events: BTN_MODE_PRESS, CMD_SET_MODE_MONITOR, CMD_SET_MODE_GAME
```

Concrete structure:

```cpp
enum SystemMode { MODE_MONITORING, MODE_GAME };
SystemMode currentMode = MODE_MONITORING;

void loop() {
    readInputs();          // buttons, numpad — always runs
    parseSerial();         // consume incoming Serial bytes — always runs
    updateTimers();        // millis-based scheduler tick — always runs

    switch (currentMode) {
        case MODE_MONITORING: runMonitoringTick(); break;
        case MODE_GAME:       runGameTick();       break;
    }

    updateOutputs();       // LCD, 7-seg, buzzer, LEDs — always runs
}
```

Key rule: `loop()` itself contains NO `delay()` calls. Every sub-function must return within microseconds.

### 1.2 Non-Blocking Peripheral Handling (millis() Scheduler)

Replace all `delay(N)` with a delta-time pattern. Each periodic task gets its own interval and last-run timestamp.

```cpp
// Timing configuration (all in milliseconds)
const unsigned long SENSOR_INTERVAL    = 100;   // HC-SR04 read: 10 Hz
const unsigned long DISPLAY_INTERVAL   = 200;   // LCD/7-seg refresh: 5 Hz
const unsigned long SERIAL_TX_INTERVAL = 200;   // outbound Serial: 5 Hz
const unsigned long LED_BLINK_INTERVAL = 500;   // status LED blink: 2 Hz

struct Task {
    unsigned long interval;
    unsigned long lastRun;
    void (*handler)();
};

Task tasks[] = {
    { SENSOR_INTERVAL,    0, readUltrasonicSensor },
    { DISPLAY_INTERVAL,   0, updateLocalDisplays  },
    { SERIAL_TX_INTERVAL, 0, transmitSensorData   },
    { LED_BLINK_INTERVAL, 0, updateStatusLED      },
};

void updateTimers() {
    unsigned long now = millis();
    for (auto& t : tasks) {
        if (now - t.lastRun >= t.interval) {
            t.lastRun = now;
            t.handler();
        }
    }
}
```

HC-SR04 special case: the ultrasonic trigger pulse and echo wait can block for up to 30 ms at max range. Use a two-phase approach — trigger on one tick, read echo on the next — or cap the timeout via `pulseIn(pin, HIGH, 20000)` (20 ms max = ~340 cm max range), which is acceptable for a demo system.

### 1.3 I2C Bus Strategy

Arduino Nano has one hardware I2C bus (A4=SDA, A5=SCL). Devices confirmed on I2C:
- LCD (PCF8574 backpack): typically address 0x27 or 0x3F
- MAX7219 LED matrix: SPI, NOT I2C — uses pins 10(CS), 11(MOSI), 13(SCK)
- TM1637 7-segment: uses its own 2-wire protocol on any 2 digital pins (not standard I2C)
- Numpad: if I2C variant (e.g. PCF8574-based), adds another address on the bus

I2C address collision prevention: scan the bus at startup with `Wire.beginTransmission()` / `Wire.endTransmission()` and print addresses to Serial. Reserve addresses before ordering hardware.

### 1.4 Pin Assignment Strategy for Arduino Nano

Arduino Nano has 14 digital pins (D0–D13) and 8 analog pins (A0–A7, usable as digital).

Recommended allocation:

| Pin(s)     | Peripheral            | Protocol     | Notes |
|------------|-----------------------|--------------|-------|
| D0, D1     | USB Serial (reserved) | UART         | Do NOT use — conflicts with USB |
| D2         | IR sensor             | Digital IN   | Interrupt-capable (INT0) |
| D3         | Mode button           | Digital IN   | Interrupt-capable (INT1), use INPUT_PULLUP |
| D4, D5     | HC-SR04 (Trig, Echo)  | Digital      | Echo: D5 (no interrupt needed with pulseIn) |
| D6         | Buzzer                | PWM/Digital  | tone() works on D6 |
| D7, D8     | Status LEDs           | Digital OUT  | Two LEDs: normal / warning |
| D9, D10    | Numpad rows 1-2       | Digital      | If matrix keypad (4x4 = 4 rows + 4 cols) |
| D11, D12   | Numpad rows 3-4       | Digital / SPI| D11=MOSI (SPI shared with MAX7219) |
| D13        | MAX7219 CLK           | SPI          | Also onboard LED — avoid LED conflict |
| A0, A1     | Numpad cols 1-2       | Digital IN   | Pull-up, read cols |
| A2, A3     | Numpad cols 3-4       | Digital IN   | Pull-up |
| A4 (SDA)   | I2C bus               | I2C          | LCD + optional I2C Numpad |
| A5 (SCL)   | I2C bus               | I2C          | |
| A6, A7     | (analog input only)   | Analog IN    | Cannot be used as digital output |

Notes:
- MAX7219 is SPI: CS on D10, MOSI on D11, CLK on D13. Do NOT use hardware SPI simultaneously for other devices unless chip-select logic is added.
- If using TM1637 for 7-segment: use any 2 free digital pins (e.g. A2, A3 freed from numpad if numpad is I2C-based).
- Matrix numpad (4×4) needs 8 pins total. Consider I2C numpad module to free 6–8 digital pins.
- With all peripherals populated, pin budget is very tight. Prefer I2C variants for Numpad and 7-seg.

### 1.5 Serial Protocol Design

Use newline-terminated (`\n`) text frames. JSON is human-readable and easy to parse host-side; a compact CSV alternative saves SRAM.

Recommendation: JSON for outbound data frames (readability, easy for Node-RED), single-word commands for inbound (minimal parsing code on 2KB SRAM Arduino).

**Arduino → Host (sensor data frame, 10 Hz):**
```json
{"t":"data","mode":"monitor","dist":123,"ir":1,"ts":45231}\n
```
Fields:
- `t`: frame type — `"data"` | `"event"` | `"ack"` | `"score"`
- `mode`: `"monitor"` | `"game"`
- `dist`: HC-SR04 distance in cm (integer)
- `ir`: IR sensor state (0/1)
- `ts`: `millis()` value (for latency diagnostics)

**Arduino → Host (game score event):**
```json
{"t":"score","score":42,"ts":98732}\n
```

**Host → Arduino (command frame):**
```
CMD:SET_MODE:GAME\n
CMD:SET_MODE:MONITOR\n
CMD:BUZZ:500\n
CMD:LED:WARNING\n
CMD:LED:NORMAL\n
```

Simple prefix parsing on Arduino:
```cpp
void parseSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            rxBuffer[rxPos] = '\0';
            dispatchCommand(rxBuffer);
            rxPos = 0;
        } else if (rxPos < RX_BUF_SIZE - 1) {
            rxBuffer[rxPos++] = c;
        }
    }
}
```

Keep `RX_BUF_SIZE` at 32–48 bytes to avoid SRAM exhaustion. Commands are short; this is sufficient.

**JSON output on Arduino:** Use `snprintf()` into a char buffer rather than the `ArduinoJson` library. ArduinoJson adds ~2KB RAM overhead which is unacceptable on a 2KB SRAM device. Hand-format:
```cpp
char outBuf[96];
snprintf(outBuf, sizeof(outBuf),
    "{\"t\":\"data\",\"mode\":\"monitor\",\"dist\":%d,\"ir\":%d,\"ts\":%lu}\n",
    distCm, irState, millis());
Serial.print(outBuf);
```

---

## 2. Host-Side Architecture

### 2.1 Node-RED (Recommended Path — Fastest to Working Dashboard)

Node-RED is the fastest path. It provides serial-in, JSON parsing, WebSocket push, and a dashboard UI as pre-built nodes.

Flow structure:

```
[serial in node]  →  [json parse node]  →  [switch node: t==data?]  →  [dashboard gauge/chart]
                                          →  [switch node: t==score?] →  [dashboard text]

[dashboard button] →  [function node: format CMD string]  →  [serial out node]
[dashboard button] →  [function node: format CMD string]  →  [serial out node]
```

Key nodes needed:
- `node-red-node-serialport` — serial read/write (install via Manage Palette)
- `node-red-dashboard` — gauge, chart, text, button widgets (install via Manage Palette)
- Built-in: `json`, `switch`, `function`, `debug`

Serial node configuration: match baud rate (115200 recommended over 9600 for reliability), delimiter = `\n`, output as string.

Flow decomposition into sub-flows:
1. **RX Flow:** serial in → json → route by `msg.payload.t` → update dashboard
2. **TX Flow:** dashboard button → function (build CMD string) → serial out
3. **Debug Flow:** wire a `debug` node after serial in to see raw frames during development

WebSocket: Node-RED dashboard uses its own WebSocket internally. No manual WebSocket setup needed.

### 2.2 Python Alternative (pyserial + FastAPI + WebSocket)

Use if Node-RED is unavailable or more control is needed.

```
[Arduino] --(USB Serial)-- [pyserial read thread]
                                    |
                             asyncio Queue
                                    |
                         [FastAPI WebSocket handler]
                                    |
                         [Browser: JS WebSocket client]
```

Concrete layer breakdown:

**serial_reader.py** — dedicated thread (not async — pyserial is blocking):
```python
import serial, threading, asyncio, json

class SerialReader(threading.Thread):
    def __init__(self, port, baud, queue: asyncio.Queue, loop):
        super().__init__(daemon=True)
        self.ser = serial.Serial(port, baud, timeout=1)
        self.queue = queue
        self.loop = loop

    def run(self):
        while True:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                try:
                    data = json.loads(line)
                    asyncio.run_coroutine_threadsafe(
                        self.queue.put(data), self.loop)
                except json.JSONDecodeError:
                    pass  # ignore malformed frames
```

**main.py** — FastAPI app with WebSocket broadcast:
```python
from fastapi import FastAPI, WebSocket
from fastapi.staticfiles import StaticFiles
import asyncio

app = FastAPI()
clients: set[WebSocket] = set()
serial_queue: asyncio.Queue = asyncio.Queue()

@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await ws.accept()
    clients.add(ws)
    try:
        while True:
            cmd = await ws.receive_text()  # commands from browser
            serial_writer.send(cmd)        # forward to Arduino
    except:
        clients.discard(ws)

async def broadcast_loop():
    while True:
        data = await serial_queue.get()
        dead = set()
        for ws in clients:
            try:
                await ws.send_json(data)
            except:
                dead.add(ws)
        clients -= dead

@app.on_event("startup")
async def startup():
    loop = asyncio.get_event_loop()
    reader = SerialReader("/dev/ttyUSB0", 115200, serial_queue, loop)
    reader.start()
    asyncio.create_task(broadcast_loop())

app.mount("/", StaticFiles(directory="static", html=True))
```

**Browser (static/index.html):** Standard JS WebSocket client. Chart.js for live graphs. On connect, open `ws://localhost:8000/ws`, parse incoming JSON, push to chart datasets.

### 2.3 Serial Buffer Overflow and Lost Packet Handling

**On the Arduino side:**
- Send at 10 Hz (100 ms interval). At 115200 baud, a 96-byte JSON frame takes ~8 ms to transmit — well within budget.
- Use 115200 baud, not 9600. At 9600 baud, a 96-byte frame takes 80 ms, leaving only 20 ms margin and risking overlap.
- Do not send data in game mode if game logic consumes too many cycles; reduce to 2 Hz in game mode.

**On the host side (Node-RED):**
- Node-RED serial node maintains its own buffer and processes line by line. Buffer overflow is rare at 10 Hz JSON.
- If frames are malformed (power glitch, reconnect), the `json` node outputs an error on `msg.error`. Catch with a `switch` node checking `msg.error` existence.

**On the host side (Python):**
- `readline()` with `timeout=1` prevents blocking forever on a hung serial port.
- JSON parse errors are caught and discarded — never crash the reader thread on bad data.
- If the serial port disconnects, wrap `serial.Serial()` in a reconnect loop with 2-second backoff.

**Protocol-level resilience:**
- Every frame ends with `\n`. A partial frame (caused by Arduino reset mid-transmission) will fail JSON parsing and be discarded. This is correct behavior.
- No sequence numbers needed for a 10 Hz demo system. If strict reliability is needed, add a rolling `seq` field and detect gaps on the host.

---

## 3. Data Flow

### 3.1 Sensor → Browser (upward flow)

```
HC-SR04 / IR Sensor
        |
   [Arduino loop() - readInputs() - every 100 ms]
        |
   [Arduino loop() - transmitSensorData() - every 200 ms]
        |  snprintf → Serial.print (newline-terminated JSON)
        |
   [USB Serial cable - 115200 baud]
        |
   [Host: serial port - COM3 / /dev/ttyUSB0]
        |
   +-----------+-----------+
   |                       |
[Node-RED serial in]   [Python SerialReader thread]
        |                       |
   [json parse]          [asyncio.Queue]
        |                       |
   [switch by t]        [FastAPI broadcast_loop]
        |                       |
   [dashboard widgets]   [WebSocket → Browser JS]
        |                       |
   [Browser via         [Browser: Chart.js / DOM update]
    Node-RED built-in WS]
```

Latency budget (sensor → browser):
- Arduino sampling: 0–100 ms (depends on schedule phase)
- Serial transmit: ~0.7 ms (96 bytes at 115200)
- Host processing: <5 ms
- WebSocket push: <5 ms (LAN/localhost)
- Total: ~110–210 ms end-to-end latency. Acceptable for environmental monitoring.

### 3.2 Browser → Arduino (downward flow)

```
Browser button click
        |
   [JS: ws.send("CMD:SET_MODE:GAME")]
        |
   [WebSocket frame → Host]
        |
   +-----------+-----------+
   |                       |
[Node-RED dashboard btn]  [FastAPI /ws receive_text()]
        |                       |
   [function node:        [serial_writer.write()]
    msg.payload = "CMD:..."]
        |
   [serial out node]
        |
   [USB Serial cable]
        |
   [Arduino: SerialEvent / parseSerial()]
        |
   [dispatchCommand() → mode change / buzzer / LED]
        |
   [Arduino sends {"t":"ack","cmd":"SET_MODE","ts":...}\n]
        |
   [Host receives ack, optionally shows confirmation in UI]
```

Round-trip latency: <50 ms on localhost (dominated by WebSocket framing and serial write).

---

## 4. Component Boundaries

### 4.1 What Lives on Arduino

| Responsibility | Rationale |
|----------------|-----------|
| Sensor polling (HC-SR04, IR) | Hardware-coupled, real-time timing required |
| Local display output (LCD, 7-seg, LED matrix) | Hardware-coupled, I2C/SPI directly driven |
| Game logic and input scanning (Numpad, buttons) | Low-latency interaction, hardware-coupled |
| Mode state machine | Cannot depend on host being connected |
| Buzzer tones (tone()) | Hardware-coupled, timing-critical |
| Serial framing and transmission | Transport layer |
| Command parsing and dispatch | Receiving side of transport layer |

Arduino does NOT do: data persistence, chart rendering, WebSocket, HTTP, authentication, logging.

### 4.2 What Lives on Host

| Responsibility | Rationale |
|----------------|-----------|
| Serial port management | OS-level access |
| JSON parsing of incoming frames | Saves Arduino SRAM (parse on host, not on device) |
| WebSocket server | Browser connectivity |
| Dashboard UI (charts, gauges, buttons) | Browser rendering |
| Command routing (button click → serial write) | Glue between browser and serial |
| Optional: historical data log to file/CSV | Host has unlimited storage |

### 4.3 Serial Protocol Boundary (Contract)

The Serial line is the **only** coupling between Arduino and host. Defined contract:

**Upward frames (Arduino → Host):**

| Frame Type | Key Fields | Frequency |
|------------|------------|-----------|
| `data` | `mode`, `dist`, `ir`, `ts` | 10 Hz in monitor mode, 2 Hz in game mode |
| `score` | `score`, `ts` | On game end event |
| `ack` | `cmd`, `ts` | On command receipt |
| `event` | `event`, `val`, `ts` | IR trigger, button press events |

**Downward commands (Host → Arduino):**

| Command | Effect |
|---------|--------|
| `CMD:SET_MODE:MONITOR` | Switch to monitoring mode |
| `CMD:SET_MODE:GAME` | Switch to game mode |
| `CMD:BUZZ:<ms>` | Trigger buzzer for N milliseconds |
| `CMD:LED:NORMAL` | Set status LED to normal |
| `CMD:LED:WARNING` | Set status LED to warning |

Both sides must tolerate unknown frame types and unknown commands gracefully (ignore, do not crash).

---

## 5. Recommended Build Order

Build order follows the dependency chain: hardware first, protocol second, host UI last. Never build UI before the data source exists.

### Phase 1 — Arduino Core (no host needed)
**Goal:** Prove hardware wiring and non-blocking loop works.

1. Bare `loop()` with millis() scheduler, Serial output of heartbeat `{"t":"data","dist":0,"ir":0,"ts":N}`
2. HC-SR04 reading integrated into scheduler
3. IR sensor reading integrated
4. Serial command parsing (`CMD:BUZZ:500` triggers buzzer)
5. Mode button → state machine toggle (monitor ↔ game stub)
6. LCD shows current mode and last distance reading

Validation: Open Arduino Serial Monitor, see JSON at 10 Hz, send `CMD:BUZZ:500`, hear buzz.

### Phase 2 — Serial Protocol Hardening
**Goal:** Confirm framing is robust before building host logic on top.

1. Test with 115200 baud on both sides
2. Stress-test with continuous data + commands simultaneously
3. Verify Arduino recovers cleanly from host disconnect/reconnect (no SRAM leak from buffer)
4. Document final field names in protocol — freeze the contract before host development

### Phase 3 — Host Serial Reader + WebSocket
**Goal:** Get data into browser as raw JSON, no dashboard yet.

Node-RED path:
1. Install `node-red-node-serialport`
2. Build serial in → json → debug flow
3. Verify JSON appears in debug panel at 10 Hz
4. Add serial out → test command sending from inject node

Python path:
1. Implement SerialReader thread
2. Implement FastAPI + WebSocket broadcast loop
3. Build minimal `index.html` that logs incoming JSON to browser console
4. Test command send from browser console `ws.send("CMD:BUZZ:500")`

### Phase 4 — Dashboard UI
**Goal:** Replace raw debug output with a usable dashboard.

1. Distance gauge/chart (live updating)
2. IR state indicator
3. Mode display (monitor/game)
4. Mode switch buttons (send SET_MODE commands)
5. Manual buzzer trigger button

### Phase 5 — Game Mode
**Goal:** Implement at least one playable game on the 8×8 matrix.

1. Define game (snake, reaction timer, or similar — LED matrix-centric)
2. Numpad input scanning integrated into game tick
3. Game state machine within `runGameTick()`
4. Score transmission via `{"t":"score",...}` frame
5. Score display on dashboard

### Phase 6 — Integration Polish
**Goal:** System works as a coherent demo.

1. LED matrix status icons in monitor mode
2. 7-segment shows distance numerically
3. Dashboard shows game score history
4. Edge case handling: Arduino reset while host running, host restart while Arduino running

---

## Dependency Graph

```
Arduino millis() scheduler
        |
        +-- Sensor polling  ─────────────────────────────────────────────┐
        +-- Local displays                                                |
        +-- Mode state machine                                            |
        +-- Serial TX (depends on sensor values)                         |
        +-- Serial RX + command dispatch                                  |
                                                                          |
Serial protocol contract (frozen in Phase 2)                             |
        |                                                                 |
        +-- Host serial reader (depends on protocol)      <──────────────┘
                |
                +-- WebSocket server (depends on reader)
                        |
                        +-- Dashboard UI (depends on WebSocket)
                                |
                                +-- Game score display (depends on game mode on Arduino)
```

**Critical path:** Arduino scheduler → Serial protocol → Host reader → WebSocket → Dashboard.
Do not start Phase 3 before Phase 2 validation. Do not build the game UI before game data is transmitted.

---

## Sources and Confidence

| Claim | Confidence | Basis |
|-------|------------|-------|
| millis() scheduler pattern | HIGH | Standard Arduino community pattern, documented in Arduino reference |
| HC-SR04 pulseIn() timeout cap | HIGH | ATmega328P datasheet + Arduino pulseIn() documentation |
| ArduinoJson SRAM cost on 328P | HIGH | ArduinoJson v6 documentation explicitly warns about 2KB SRAM constraint |
| MAX7219 is SPI not I2C | HIGH | MAX7219 datasheet |
| TM1637 is not standard I2C | HIGH | TM1637 datasheet — proprietary 2-wire protocol |
| Node-RED serialport node availability | HIGH | node-red-node-serialport is maintained by Node-RED project |
| FastAPI + asyncio + threading serial pattern | HIGH | pyserial docs, FastAPI docs |
| 115200 baud frame timing calculation | HIGH | Standard serial bit-timing arithmetic |
