# Research Summary: Arduino Nano Dual-Mode IoT System

**Synthesized:** 2026-05-19
**Source files:** STACK.md, FEATURES.md, ARCHITECTURE.md, PITFALLS.md
**Consumer:** gsd-roadmapper agent

---

## Executive Summary

This is an embedded + web full-stack project on severely constrained hardware: an ATmega328P with 2KB SRAM driving 7+ peripherals over SPI, I2C, and UART simultaneously. The dominant technical challenge is not any single feature -- it is resource contention. Pin budget, SRAM budget, CPU time budget, and USB power budget are all tight, and any of them can silently break the system in ways that look like software bugs. Every architectural decision flows from this constraint.

The recommended approach is incremental hardware bringup (one peripheral at a time, verified before adding the next), a millis()-based non-blocking loop as the firmware spine, and a frozen serial protocol as the integration contract between firmware and the web host. The web stack is Python + FastAPI + WebSocket + vanilla JS, chosen over Node-RED because the bidirectional command protocol and custom game dashboard require real code, not flow wiring. Snake is the recommended game: it is the canonical 8x8 application, fits in approximately 64 bytes of SRAM for body state, and maps naturally to numpad directional input.

The highest-risk pitfalls are SRAM exhaustion via Arduino String class (causes mysterious random crashes after 30-60 seconds), pulseIn() blocking the game loop (causes missed inputs and serial command drops), and late integration (serial protocol mismatch discovered the night before the demo). All three are preventable by design decisions made in Phases 1 and 2 and cannot be patched later without significant refactoring.

---

## 1. Recommended Stack

### Arduino Libraries

| Library | Version | Purpose | Decision Rationale |
|---------|---------|---------|-------------------|
| NewPing | 1.9.7 | HC-SR04 ultrasonic | Non-blocking ping via timer interrupt; raw pulseIn() blocks up to 30ms and kills the game loop |
| MD_MAX72XX | 3.5.1 | MAX7219 8x8 matrix pixel control | De-facto standard; direct setPoint()/setRow() access needed for game rendering |
| MD_Parola | 3.7.3 | MAX7219 text scroll (monitor mode) | Built on MD_MAX72XX; ready-made scroll/wipe animations for mode entry |
| LiquidCrystal_I2C | 1.1.2 | I2C LCD | Universal PCF8574 backpack driver; saves 6 GPIO pins vs parallel wiring |
| TM1637Display | 1.2.0 | 7-segment display | Only if TM1637 module (2 GPIO pins); if 74HC595 + bare segment, use shiftOut() with no library |
| Keypad | 3.1.1 | 4x4 numpad matrix scan | Handles ghost key prevention, debounce, non-blocking getKey() poll |
| IRremote | 4.4.0 | IR remote decode | Only if IR sensor is a remote receiver (VS1838B/TSOP type). Proximity module = digitalRead() only |

**Critical exclusions:**
- No ArduinoJson: adds approximately 2KB RAM overhead on a 2KB SRAM device; use snprintf() into char[] instead
- No Arduino String class: heap fragmentation causes crashes after approximately 60 seconds of runtime
- No LedControl: superseded by MD_MAX72XX
- No raw pulseIn() for HC-SR04: blocks CPU for up to 30ms

### Serial Protocol

- **Format:** JSON outbound, plain-text commands inbound, newline-terminated, 115200 baud
- **Arduino to Host (sensor):** {"t":"data","mode":"monitor","dist":123,"ir":1,"ts":45231}
- **Arduino to Host (game end):** {"t":"score","score":42,"ts":98732}
- **Host to Arduino:** CMD:SET_MODE:GAME  |  CMD:BUZZ:500  |  CMD:LED:WARNING
- **Rationale:** JSON is self-describing and natively parsed by Python/JS. Plain-text inbound commands keep Arduino RX parsing to a simple prefix match with no JSON parser needed on the constrained device.
- **Why 115200 baud:** At 9600 baud a 96-byte frame takes 80ms leaving only 20ms margin before overlap. At 115200 it takes approximately 8ms.

### Web Stack

| Layer | Technology | Rationale |
|-------|-----------|----------|
| Serial bridge | pyserial 3.5 | Standard Python serial; readline() gives clean newline framing |
| Web framework | FastAPI 0.111+ | Async-native WebSocket; cleaner serial bridge than Flask with no manual threading boilerplate |
| ASGI server | uvicorn 0.29+ | Single-command runner; handles concurrent WebSocket + HTTP |
| Frontend | Vanilla HTML + JS | No build step; works in any browser; no bundler complexity |
| Charting | Chart.js 4.x CDN | Real-time streaming via chart.update with no-animation mode + rolling data.shift() |

**Node-RED is excluded:** Game command logic requires real code (not Function-node JS in a text box), Dashboard 2.0 layout is constrained for a combined sensor+game UI, and Windows serialport installation via node-gyp is a recurring failure point.

---

## 2. Table Stakes Features

These must be present for the project to pass evaluation. Absence of any one signals an incomplete system.

**Arduino firmware:**
- Non-blocking loop() with millis()-based scheduler; zero delay() calls in main loop
- Dual-mode state machine (MONITORING / GAME) with clean onEnter/update/onExit transitions
- HC-SR04 distance reading at 10 Hz in monitor mode, reduced to 2 Hz in game mode
- IR sensor state included in every data frame
- LCD showing current mode and last key sensor value
- 7-segment showing score in game mode or distance in monitor mode
- Serial JSON output at 5-10 Hz, 115200 baud, newline-terminated

**Web dashboard:**
- Live distance chart (real-time line chart, rolling 60-second window)
- IR sensor status badge (green/red visual state, not just a number)
- Current mode indicator (monitor vs game)
- Last updated timestamp proving the connection is alive
- Bidirectional: at least one command from browser to Arduino (mode switch button minimum)

**Game (Snake -- required for full marks):**
- Complete game loop: start screen, play, game over, score display, restart
- Score shown on 7-segment during and after game
- Game-over event sent to web dashboard: {"t":"score","score":N}
- Responsive numpad input with no perceptible lag

---

## 3. Recommended Game: Snake

**Decision: Snake is the primary game. Reaction timer is the fallback.**

| Game | Verdict | Reason |
|------|---------|--------|
| Snake | YES -- primary | Canonical 8x8 application; 64-byte body array; numpad direction keys map naturally; universally understood |
| Reaction timer | YES -- fallback | Approximately 2 hours to implement; stateless; build as insurance if Snake hits a blocker |
| Tetris | NO | 10-wide board cramped to 8 columns; 28 bitmasks for pieces; effort disproportionate to constrained payoff |
| Space Invaders | NO | Entity density too low on 8x8; 2 rows for aliens is not recognizable as the game |

Snake SRAM cost: 64-byte body array, direction byte, length byte, food position = approximately 70 bytes total game state. Fits within the approximately 500 bytes allocated to application code after library overhead.

---

## 4. Architecture in Brief

### Four Non-Negotiable Patterns

**1. Non-blocking millis() scheduler (firmware spine)**
Every periodic task (sensor read, Serial TX, display update, game tick) runs on a delta-time interval. The top-level loop() contains zero delay() calls. Each task gets lastRun + interval variables and executes only when millis() - lastRun >= interval.

**2. Explicit state machine (not boolean flags)**

    enum SystemState { STATE_MONITOR, STATE_GAME, STATE_GAME_OVER };

Each state has onEnter() for initialization, update() for per-loop logic, and onExit() for cleanup. Mode switches call these explicitly. Scattering if(gameMode) checks across every function leads to an unextendable codebase.

**3. Newline-framed serial protocol with frozen schema**
The protocol spec is written before any code is written. It is the integration contract. snprintf() into char[96] on Arduino. Never String class, never ArduinoJson.

**4. Threading model on host (Python)**
Serial reader runs in a dedicated daemon thread with timeout=1 on serial.Serial(). It populates an asyncio.Queue. FastAPI event loop reads from the queue and broadcasts to WebSocket clients. Main thread never blocks on serial. Serial thread never touches WebSocket clients directly.

### Component Boundaries

- **Arduino owns:** sensor polling, local display output, game logic, mode state machine, buzzer, serial framing
- **Host owns:** serial port management, JSON parsing, WebSocket server, dashboard UI, command routing, optional CSV logging
- **Serial line is the only coupling.** Arduino must function standalone even if the host is disconnected.

### Pin Strategy Summary

MAX7219 uses SPI: D10 CS, D11 MOSI, D13 CLK. I2C bus (A4/A5) for LCD. TM1637 on any 2 free digital pins. Matrix numpad needs 7-8 GPIO; if pin budget is tight, use I2C numpad module on the I2C bus (frees 6-7 pins). A6/A7 cannot be used as digital outputs. D0/D1 are reserved for USB serial.

---

## 5. Top Pitfalls to Avoid

| # | Pitfall | Severity | One-line Prevention |
|---|---------|----------|-------------------|
| 1 | Arduino String class and heap fragmentation | CRITICAL | Never use String; always char buf[N] + snprintf() + F() for string literals |
| 2 | pulseIn() blocking the game loop | CRITICAL | Use NewPing from day one; 30ms block at 10 Hz = 30% of CPU in a blocking wait |
| 3 | delay() in loop() | HIGH | Adopt millis()-based scheduler in Phase 1 before any feature code |
| 4 | Pin budget exhaustion mid-project | HIGH | Draw pin assignment table before wiring anything; TM1637 saves 6 pins vs bare 7-seg |
| 5 | Late integration and serial protocol mismatch | HIGH | Write protocol spec as a Phase 2 deliverable; never develop dashboard against hardcoded test data |
| 6 | I2C address conflict (LCD and numpad both at 0x27) | MEDIUM | Run i2c_scanner as the very first firmware task before any library initialization |
| 7 | USB power budget exceeded (500mA limit) | MEDIUM | Limit MAX7219 brightness to 30-40% via setIntensity(); add 100uF cap near MAX7219 power pins |
| 8 | Baud rate mismatch producing garbage symbols | MEDIUM | Define BAUD_RATE 115200 in config.h; use same constant in Python; test with Serial Monitor first |

---

## 6. Open Questions

These hardware decisions are unresolved. They must be answered with hardware in hand before Phase 1 wiring begins. They do not block planning -- they create conditional branches that resolve immediately on hardware inspection.

| # | Question | Impact |
|---|----------|--------|
| 1 | IR sensor type: digital proximity module (TCRT5000/FC-51) or IR remote receiver (VS1838B/TSOP38238)? | Proximity = digitalRead() only, no library. Remote = install IRremote v4, must resolve Timer2 conflict with NewPing. |
| 2 | 7-segment type: TM1637 module (2 GPIO pins) or bare 7-segment + 74HC595 (3 GPIO pins)? | TM1637 = TM1637Display library. 74HC595 = shiftOut(), no library. Pin budget and code path differ. |
| 3 | Numpad type: standard 4x4 matrix (7-8 GPIO pins) or I2C PCF8574 module (0 extra GPIO)? | Matrix = verify pin map against SPI/I2C allocation. I2C = verify address does not conflict with LCD at 0x27. |
| 4 | Timer conflict: if IRremote + NewPing async mode are both active, both claim AVR Timer2. | One library will silently malfunction. Check USE_TIMER defines in each library header before combining. |
| 5 | Windows COM port name for Python configuration. | Hardcoded COM3 breaks on other machines or USB ports. Use .env file or CLI argument, never hardcode. |

---

## 7. Suggested Build Order

Build order follows the dependency chain: firmware before protocol, protocol frozen before host code, host serial bridge validated before dashboard UI. Never build the UI before the data source is validated.

### Phase 1 -- Hardware Bringup and Firmware Foundation

Deliverables: All peripherals verified independently; millis() scheduler in place; no String class, no delay() in loop.

1. Create pin assignment table (paper or doc) resolving open hardware questions
2. Serial echo sketch: heartbeat JSON at 115200 baud proving USB serial chain works
3. Bringup each peripheral in isolation in this order: HC-SR04, IR sensor, LCD, MAX7219, 7-segment, Buzzer, Mode button, Numpad
4. Establish coding conventions: char[] + snprintf(), F() macros, millis() scheduler skeleton applied from the first commit
5. Power test with all peripherals enabled simultaneously before writing any application logic

Research flag: None -- peripheral bringup follows standard library documentation.

### Phase 2 -- Serial Protocol and State Machine

Deliverables: Frozen protocol spec document; working enum SystemState state machine; non-blocking sensor reads.

1. Write serial protocol spec (field names, frame types, command syntax) and freeze it as a shared document before any host code is written
2. Implement enum SystemState state machine with onEnter/update/onExit per state
3. Integrate all sensors into millis() scheduler; verify JSON output at 10 Hz in monitor mode via Serial Monitor
4. Implement command parser (CMD: prefix) and serial RX buffer (char[48])
5. Integration smoke test: Serial Monitor at 115200, send commands, verify correct responses

Research flag: None -- state machine and millis() patterns are standard and well-documented.

### Phase 3 -- Host Serial Bridge and WebSocket

Deliverables: Data flowing from Arduino to browser console; commands flowing from browser to Arduino; reconnection handled.

1. Python SerialReader daemon thread + asyncio Queue + FastAPI WebSocket broadcast
2. Minimal index.html logging incoming JSON to browser console
3. Test ws.send from browser console to trigger Arduino buzzer
4. Add heartbeat ping/pong + reconnection with exponential backoff + Disconnected banner
5. Build mock serial emitter (Python script) to decouple UI development from hardware availability

Research flag: Low -- pattern is documented. Verify current node-red-node-serialport behavior only if switching to Node-RED path.

### Phase 4 -- Dashboard UI

Deliverables: Usable monitoring dashboard with live charts and bidirectional controls.

1. Distance line chart (Chart.js, rolling 60-second window, update with no-animation flag + shift())
2. IR sensor status badge (green/red)
3. Mode indicator and mode switch buttons
4. Last updated timestamp
5. Manual buzzer trigger button

Research flag: None -- Chart.js 4.x real-time pattern is standard.

### Phase 5 -- Snake Game

Deliverables: Complete playable Snake game with score reporting to web dashboard.

1. Game tick function in runGameTick() within the state machine
2. Numpad input scanning (2/4/6/8 = direction)
3. Snake body as ring buffer (64-byte array), food placement, collision detection
4. Score on 7-segment during game; score JSON frame on game over via Serial
5. STATE_GAME_OVER state; restart on numpad press

Research flag: None -- Snake on 8x8 is a canonical pattern with well-understood implementation.

### Phase 6 -- Integration Polish

Deliverables: Demo-ready system with edge case handling.

1. MD_Parola scroll animation on mode entry; LED matrix status icons in monitor mode
2. Game score history on web dashboard (in-memory JS array, session lifetime)
3. Edge case handling: Arduino reset while host running; host restart while Arduino running
4. Optional differentiators: distance-controlled game speed, high-score leaderboard, CSV export button

Research flag: None -- integration work, not new domain research.

---

## Confidence Assessment

| Area | Confidence | Basis |
|------|------------|-------|
| Stack: Arduino libraries | HIGH | Established, well-documented libraries; stable as of Aug 2025 knowledge cutoff |
| Stack: library version numbers | MEDIUM | Verify in Arduino Library Manager before pinning -- numbers may have incremented |
| Stack: Python/FastAPI/WebSocket | HIGH | FastAPI WebSocket API stable since 0.100+; asyncio serial bridge is a documented pattern |
| Stack: Node-RED rejection rationale | HIGH | Based on documented architecture constraints, not preference |
| Features: table stakes list | HIGH | Derived from fixed hardware constraints (8x8 matrix, 2KB SRAM) which are datasheet facts |
| Game recommendation (Snake) | HIGH | 8x8 canonical size; SRAM cost is deterministic math |
| Architecture patterns | HIGH | millis() scheduler and state machine are standard Arduino community patterns |
| Pitfalls: hardware (SRAM, pins, power) | HIGH | ATmega328P datasheet facts; USB 2.0 spec |
| Pitfalls: Node-RED/pyserial specifics | MEDIUM | Training knowledge; verify node-red-node-serialport current behavior against docs |
| Open hardware questions | N/A | Cannot resolve without physical hardware in hand |

**Overall: HIGH confidence for all design and architecture decisions. The unresolved hardware questions do not block planning -- they create conditional branches in Phase 1 that resolve immediately on hardware inspection.**

---

## Sources (Aggregated)

- ATmega328P datasheet: 2KB SRAM, 32KB Flash, 1KB EEPROM (HIGH)
- Arduino Nano pinout: D0-D13, A0-A7, A6/A7 analog-only (HIGH)
- NewPing library by Tim Eckhoff: non-blocking HC-SR04 via timer interrupt (HIGH, training data Aug 2025)
- MD_MAX72XX / MD_Parola by MajicDesigns: SPI MAX7219 driver chain abstraction (HIGH)
- LiquidCrystal_I2C by Frank de Brabander: PCF8574 + HD44780 (HIGH)
- TM1637Display by Avishay Orpaz: proprietary 2-wire protocol (HIGH)
- Keypad by Mark Stanley and Alexander Brevig: matrix keypad scan + debounce (HIGH)
- IRremote v4.x by Armin Stumpp: v4 API change from decode_results to IrReceiver.decode() (HIGH)
- FastAPI 0.111+ WebSocket API (HIGH)
- pyserial 3.5 readline() blocking behavior (HIGH, documented in pyserial docs)
- Chart.js 4.x real-time streaming: update with no-animation + data.shift() pattern (HIGH)
- node-red-node-serialport serial node Buffer vs String and split-on-timeout default (MEDIUM, training knowledge)
- Arduino String class heap fragmentation on AVR: documented community issue for 10+ years (HIGH)
- MAX7219 power draw: up to 320mA at full brightness per Maxim datasheet (HIGH)
- USB 2.0 500mA current limit per USB specification (HIGH)
