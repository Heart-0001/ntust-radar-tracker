# Domain Pitfalls: Arduino Nano Dual-Mode IoT System

**Domain:** Arduino Nano + USB Serial + Web Dashboard (Node-RED / Python)
**Project:** NTUST 114-2 Arduino Nano 雙模式 IoT 互動系統
**Researched:** 2026-05-19
**Confidence:** HIGH for hardware pitfalls (stable, well-documented); MEDIUM for Node-RED/pyserial specifics (training knowledge, no live verification)

---

## Category 1 — Arduino Nano Hardware Pitfalls

### Pitfall H1: SRAM Exhaustion via `String` Class and Large Buffers

**What goes wrong:**
The ATmega328P has only 2KB of SRAM shared between stack, heap, and global variables. The Arduino `String` class allocates on the heap and fragments it with repeated concatenation (`String msg = "DIST:" + String(dist) + ",IR:" + String(ir)`). After a few hundred iterations the heap and stack collide silently — the program keeps running but produces garbage values, resets randomly, or corrupts unrelated variables. This is the single most common cause of mysterious Arduino instability in student projects.

**Why it happens:**
- Each `String` object calls `malloc`/`free`; heap fragmentation is non-recoverable without a full reset.
- Serial format strings (JSON, CSV labels) stored as `String` literals consume RAM even when the string is constant.
- `sprintf` into a `char[]` buffer is safe but beginners reach for `String` first.

**Warning signs:**
- Program works fine for 30–60 seconds then freezes or resets.
- Sensor readings become `0`, `65535`, or random numbers after uptime grows.
- `Serial.println(freeMemory())` (using `MemoryFree` library) shows < 200 bytes free at startup.
- Adding more features causes earlier crashes.

**Prevention:**
- Never use `String` class. Use `char buf[32]` + `snprintf(buf, sizeof(buf), "DIST:%d,IR:%d\n", dist, ir)`.
- Store all string literals in Flash with `F()` macro: `Serial.print(F("DIST:"))` saves RAM for every constant string.
- Declare large `char` buffers as global (not local) so they live in BSS, not on the stack.
- Use `MemoryFree` library during development to monitor free SRAM; keep > 300 bytes headroom at all times.
- Budget RAM at the start: LCD library ~50B, MAX7219 library ~80B, 32-byte serial buffer = ~160B before any application code. With 2KB total, overhead adds up fast.

**Phase to address:** Phase 1 (firmware foundation) — establish `char[]` + `snprintf` + `F()` conventions before writing any feature code. Retrofitting is painful.

---

### Pitfall H2: I2C Address Conflicts Between LCD and Numpad

**What goes wrong:**
The I2C LCD backpack (typically PCF8574 expander) defaults to address `0x27` or `0x3F` depending on the manufacturer. If the Numpad also uses I2C (e.g., PCF8574-based matrix scanner or MPR121), it may ship with the same default address. When two devices share an address, writes intended for the LCD corrupt the Numpad state machine and vice versa — the LCD shows garbage, the Numpad misses keypresses, and no error is thrown because `Wire.endTransmission()` returns success from whichever device responds first.

**Why it happens:**
- PCF8574 address is set by A0/A1/A2 jumpers on the PCF8574 chip; both LCD backpacks and Numpad modules often ship with all three jumpers open (address `0x27`).
- Students assume libraries handle conflict detection — they do not.

**Warning signs:**
- LCD prints incorrect characters or freezes after Numpad interaction.
- Running an I2C scanner sketch (`i2c_scanner`) shows only one device where two are expected, or both at the same address.
- Behavior differs depending on which `Wire.begin()` call happens first.

**Prevention:**
- Run `i2c_scanner` before writing any application code. Confirm each device has a unique address.
- If the Numpad module has address jumpers, physically set A0 high (solder bridge or jumper wire) to move it to `0x26` or `0x28`.
- If no address jumpers exist on the Numpad, consider switching to a matrix-scanned Numpad (no I2C, uses 7 digital pins) to eliminate the conflict entirely — simpler and uses no I2C.
- Document final I2C addresses in a comment at the top of the firmware file.

**Phase to address:** Phase 1 (hardware bringup) — scan I2C bus as the very first firmware task before integrating any library.

---

### Pitfall H3: Pin Budget Exhaustion

**What goes wrong:**
The Arduino Nano has 22 usable I/O pins (D0–D13, A0–A7) but several are pre-consumed: D0/D1 are Serial RX/TX (unavailable while USB is connected), D13 is the onboard LED (usable but conflicts with SPI), A4/A5 are I2C SDA/SCL. That leaves ~16 pins. With this project's peripherals, the budget is extremely tight.

**Rough pin count for this project:**
| Peripheral | Pins Used | Notes |
|---|---|---|
| HC-SR04 | 2 (TRIG, ECHO) | Digital |
| IR sensor | 1 | Digital |
| I2C LCD | 0 (uses A4/A5) | Already reserved |
| MAX7219 (SPI) | 3 (DIN, CLK, CS) | Can share CLK/DIN with other SPI |
| 7-segment (TM1637) | 2 | CLK + DIO |
| 7-segment (direct) | 8–11 | If not TM1637 — avoid |
| Buttons (mode switch) | 2–3 | Digital with INPUT_PULLUP |
| Numpad (matrix scan) | 7 (4 row + 3 col) | If not I2C |
| Numpad (I2C) | 0 (uses A4/A5) | Shares I2C bus |
| Buzzer | 1 | PWM preferred |
| LEDs (status) | 2–4 | Digital |
| **Total (worst case)** | **~23–26** | **Exceeds Nano capacity** |

**Warning signs:**
- Running out of free pins mid-project; last few peripherals cannot be connected.
- Needing to use A6/A7 (analog-input only on Nano — cannot be used as digital output).
- Using pin D13 for an output LED causes the onboard LED to fight it.

**Prevention:**
- Draw a pin assignment table before wiring anything. Allocate pins on paper first.
- Use TM1637 for the 7-segment display (only 2 pins) rather than direct segment driving (8 pins). This alone saves 6 pins.
- If the Numpad is I2C with a unique address, use it on the I2C bus (saves 7 pins versus matrix scan).
- Share SPI bus for MAX7219 (DIN/CLK shared with other SPI devices if added later; each needs its own CS).
- Reserve D0/D1 — never assign them to peripherals.
- Never use A6/A7 as outputs.

**Phase to address:** Phase 1 (hardware planning) — create the pin assignment table as a deliverable before any wiring.

---

### Pitfall H4: USB Power Budget Exceeded

**What goes wrong:**
USB 2.0 provides 500mA at 5V = 2.5W. The Arduino Nano's 5V pin directly passes USB 5V through a polyfuse rated ~500mA. The project's peripherals together can exceed this:
- I2C LCD backlight: ~20–40mA
- MAX7219 at full 8x8 brightness: up to 320mA (40mA × 8 columns)
- HC-SR04: ~15mA
- Buzzer (active): ~30–40mA
- LEDs (3 × 20mA): ~60mA
- Logic for all ICs: ~20mA

At full load: ~485–495mA, right at the USB limit. Voltage sag causes I2C errors, LCD glitches, and random resets — symptoms that look like software bugs.

**Warning signs:**
- System works fine on bench power supply (9V external) but behaves erratically on USB.
- LCD flickers when buzzer activates.
- Random resets that don't happen with fewer peripherals connected.
- 5V rail measures 4.6V or lower under load (should be 4.75V minimum).

**Prevention:**
- Limit MAX7219 brightness to 30–40% of maximum via `setIntensity()` (reduces to ~100–130mA).
- Add a 100µF electrolytic capacitor across the 5V/GND rail near the MAX7219 to handle transient current spikes.
- For demo/final presentation, use an external 5V 2A USB charger via the Nano's VIN (7–12V) or 5V pin from an external supply, not the host USB port.
- Never power high-current loads (motors, high-brightness LED arrays) directly from the Nano 5V pin.

**Phase to address:** Phase 1 (hardware bringup) — test with all peripherals enabled simultaneously before writing application logic.

---

### Pitfall H5: `pulseIn()` Blocking the Entire Program

**What goes wrong:**
`pulseIn(ECHO, HIGH, 30000)` blocks execution for up to 30ms (the timeout for HC-SR04 max range ~5m). During this time:
- No serial bytes are read from the host — incoming commands are dropped.
- The Numpad is not scanned — keypresses are missed.
- The game loop freezes — causes visible stuttering.
- Buzzer tones using `tone()` are interrupted.

At 10Hz sensor polling (every 100ms), up to 30% of CPU time is in a blocking wait doing nothing.

**Warning signs:**
- Numpad in game mode feels "laggy" or misses inputs.
- Serial commands from the web dashboard are acknowledged with noticeable delay.
- Adding more peripherals makes the lag worse (more blocking calls stack up).

**Prevention:**
- Use the **NewPing library** (`NewPing sonar(TRIG, ECHO, 200)`) which provides `sonar.ping_cm()` with configurable timeout and is easily adapted to non-blocking use via `ping_timer()` and a timer interrupt.
- Alternatively, implement a manual non-blocking approach using `micros()`:
  ```cpp
  // Trigger the pulse, then check ECHO pin state in loop() without blocking
  // Use a state machine: IDLE → TRIGGERED → WAITING_FOR_ECHO → MEASURING → DONE
  ```
- Reduce sensor polling rate: 5Hz (every 200ms) is sufficient for distance monitoring and cuts blocking time in half.
- Keep `pulseIn` timeout short: use `pulseIn(ECHO, HIGH, 25000)` (max ~4.3m range) rather than the default infinite timeout.

**Phase to address:** Phase 2 (sensor integration) — implement non-blocking pattern from the start, not as a refactor.

---

## Category 2 — Serial Communication Pitfalls

### Pitfall S1: Arduino Serial TX Buffer Overflow (Data Loss on High-Frequency Send)

**What goes wrong:**
`Serial.println()` on Arduino writes to a 64-byte TX hardware buffer and returns immediately — but if the sketch calls it faster than the baud rate can drain the buffer, `Serial.println()` will silently drop characters or block until space is available. At 9600 baud, maximum throughput is ~960 bytes/sec. A single JSON message like `{"dist":123,"ir":1,"mode":"monitor"}` is ~40 bytes, so max reliable rate is ~24 messages/sec. Sending at 10Hz is fine; sending a burst of messages (mode change + sensor reading + game state all at once) can overflow.

**Warning signs:**
- Host receives truncated JSON that fails to parse (`{"dist":1` instead of `{"dist":123,...}`).
- Increasing throughput makes data loss worse, not better.
- `Serial.availableForWrite()` returns small values (< 20) during normal operation.

**Prevention:**
- Use **115200 baud** instead of 9600. This increases throughput 12x (to ~11,500 bytes/sec) and virtually eliminates TX buffer overflow for this project's data rate.
- Send one message per `loop()` iteration at a fixed interval (e.g., every 100ms) rather than burst-sending multiple messages.
- Keep each message short: `D:123,I:1,M:0\n` (CSV, 14 bytes) instead of full JSON when bandwidth matters.
- If using JSON, measure the length of your longest message and verify it fits in one `Serial.println()` call below 64 bytes, or use `Serial.write()` with a pre-built buffer.

**Phase to address:** Phase 2 (serial protocol design) — choose baud rate and message format before writing any parsing code.

---

### Pitfall S2: Host Reads Partial Packets (Framing / Synchronization)

**What goes wrong:**
The host (Node-RED or Python) reads from the serial port in chunks that do not align with Arduino message boundaries. If Arduino sends `DIST:123\nIR:1\n` and the host reads in 8-byte chunks, it might receive `DIST:12` then `3\nIR:1\n` — the first read is not a valid message. This causes parse errors that appear intermittently (worse at higher data rates or under CPU load on the host).

**Why it happens:**
- TCP and serial streams have no inherent message framing.
- `readline()` in Python reads up to `\n` — works correctly if Arduino always sends `\n`-terminated lines.
- Node-RED's serial node splits on a configurable delimiter but students often leave it at default (split on timeout), which is unreliable.

**Warning signs:**
- Parse errors appear randomly, not consistently.
- Error rate increases when host is under CPU load.
- First message after connecting always fails to parse.

**Prevention:**
- Always terminate every Arduino message with `\n` (`Serial.println()` does this automatically).
- Use a **newline-delimited protocol**: one complete message per line, always. Never embed `\n` inside a message.
- On the Python side, always use `ser.readline()` (blocks until `\n`) rather than `ser.read(N)`.
- On Node-RED, configure the Serial node to split on `\n` (character `0x0A`), not on timeout.
- Add a start-of-message sentinel optionally: `>DIST:123,IR:1\n` — parser checks for `>` before accepting.
- On the Arduino side, after reset, wait 500ms before sending the first message (host port may not be open yet).

**Phase to address:** Phase 2 (serial protocol design) — establish framing convention as a written spec before any host code is written.

---

### Pitfall S3: Port Access Conflict (Arduino IDE vs Running Application)

**What goes wrong:**
Only one process can hold the serial port open at a time. If Node-RED (or the Python script) is running and has the port open, Arduino IDE cannot upload firmware — the upload fails with "avrdude: ser_open(): can't open device" or similar. Students waste 20–40 minutes diagnosing this, often blaming the Arduino or the cable.

Conversely, if the Arduino IDE Serial Monitor is open during development, Node-RED/Python cannot connect.

**Warning signs:**
- Upload fails immediately after a firmware build succeeds.
- Node-RED shows "port open" error at startup.
- Error message references the COM port name (e.g., `COM3` on Windows, `/dev/ttyUSB0` on Linux).

**Prevention:**
- Establish a workflow rule: close Node-RED / Python script before uploading firmware. Write this in the project README.
- In Node-RED, add a toggle switch to enable/disable the Serial node (avoids restarting the whole flow for uploads).
- In Python, use `try/except serial.SerialException` on port open and print a clear error message: "Close Arduino IDE Serial Monitor before running."
- On Windows, use **Device Manager** to confirm which COM port the Nano is on — it can change between USB ports.

**Phase to address:** Phase 1 (development environment setup) — document the workflow in the project README immediately.

---

### Pitfall S4: Baud Rate Mismatch Produces Garbage Output

**What goes wrong:**
If `Serial.begin(115200)` in firmware and Node-RED/Python opens at `9600`, every byte is misinterpreted. The terminal shows `???` or random Unicode — no error is thrown. Students often diagnose this as a wiring problem or a firmware bug, spending an hour on the wrong track.

**Warning signs:**
- Serial Monitor shows `???`, `ÿ`, or sequences of random symbols.
- The Arduino appears to be sending data (TX LED blinks) but content is unreadable.
- Changing to a different baud rate in the host application suddenly fixes everything.

**Prevention:**
- Pick one baud rate for the project and write it in the PROJECT.md decisions table. **115200 is recommended** (faster, same reliability on short USB cable).
- Create a `config.h` or constant at the top of the firmware: `#define BAUD_RATE 115200` — never hardcode it in `setup()`.
- On Node-RED, keep the baud rate visible on the Serial node label. On Python, keep it as a named constant: `BAUD = 115200`.
- Test baud rate with Arduino IDE Serial Monitor first before integrating host software.

**Phase to address:** Phase 1 (project setup) — one decision, documented, shared across firmware and host code.

---

## Category 3 — Web Stack Pitfalls

### Pitfall W1: Node-RED Serial Node Quirks

**What goes wrong:**
Node-RED's `node-red-node-serialport` serial node has several non-obvious behaviors that cause intermittent failures:

1. **Split behavior default:** The node defaults to splitting on a fixed timeout (character timeout), not on `\n`. This means it may deliver partial lines or bundle multiple lines into one `msg.payload`. Students assume it splits on newline because that seems logical.
2. **msg.payload is a Buffer, not a String:** By default, `msg.payload` arrives as a Node.js `Buffer` object. `JSON.parse(msg.payload)` fails silently or throws; students need to add a `toString()` call or a Function node with `msg.payload = msg.payload.toString()`.
3. **Port not found on Windows:** Node-RED may not see COM ports above COM9 without a port name workaround (`\\.\COM10`).
4. **Flow redeployment leaves port open:** Redeploying a Node-RED flow does not always cleanly close and reopen the serial port. A stale port handle causes "port already open" on the next deploy.

**Warning signs:**
- Dashboard shows `[object Object]` instead of sensor values.
- Messages arrive bunched together (two readings in one payload).
- After a Node-RED redeploy, serial data stops arriving until Node-RED is restarted.

**Prevention:**
- In the Serial node configuration, set split to "on the character `\n`" explicitly.
- Immediately after the Serial node, add a **Function node** that converts: `msg.payload = msg.payload.toString().trim(); return msg;`
- Test with a debug node attached to the raw serial output before building any dashboard UI.
- If redeployment issues arise, use the Node-RED "Restart Flows" option (not just Deploy), or restart the Node-RED process.
- Document the Node-RED flow version and `node-red-node-serialport` package version in the project README.

**Phase to address:** Phase 3 (host integration) — test serial receive and parse in isolation before connecting to any dashboard UI.

---

### Pitfall W2: pyserial Threading and `readline()` Blocking the Main Thread

**What goes wrong:**
If using Python, `ser.readline()` blocks until a newline arrives or timeout expires. If called in the main thread of a Flask/FastAPI app, it will block request handling. If called in a loop without a timeout, a disconnected Arduino freezes the entire Python process.

Additionally, if a background thread reads from the serial port AND the main thread (or another thread) also reads, they race — bytes are split between threads and neither gets complete messages.

**Warning signs:**
- Flask app becomes unresponsive while waiting for serial data.
- Sensor values stop updating when Arduino is briefly disconnected.
- Intermittent `serial.SerialException: read failed` errors in the console.

**Prevention:**
- Always read serial in a **dedicated daemon thread** that populates a `threading.Queue` or shared dictionary protected by a `threading.Lock`.
- Set a serial read timeout: `serial.Serial(port, 115200, timeout=1)` — `readline()` returns an empty bytes object after 1 second instead of blocking forever.
- Main thread (Flask/FastAPI) reads from the queue/dict; serial thread reads from the port. Never cross these.
- Handle `serial.SerialException` in the reader thread and attempt reconnection with exponential backoff.

**Phase to address:** Phase 3 (host integration) — design the threading model on paper before writing any serial + web server code.

---

### Pitfall W3: WebSocket Connection Management (Reconnection and Stale State)

**What goes wrong:**
Browser WebSocket connections drop when: the computer sleeps, the user navigates away and back, or the Node-RED/Python server restarts. The dashboard then shows stale data (last reading before disconnect) with no indication to the user that the connection is dead.

For game mode: if the WebSocket drops mid-game, the score may be lost. If the host sends a buffered "game over" event after reconnect, the client may display it out of context.

**Warning signs:**
- Dashboard shows the same sensor value for minutes without updating.
- Browser console shows WebSocket `readyState: 3` (CLOSED).
- After laptop sleep, dashboard is frozen but no error is shown.

**Prevention:**
- Implement a **heartbeat/ping-pong** mechanism: server sends `{"type":"ping"}` every 5 seconds; client responds with `{"type":"pong"}`. If 3 consecutive pings are missed, mark connection as dead and show a "Disconnected" banner.
- On the client side, implement automatic reconnection with exponential backoff (`1s → 2s → 4s → max 30s`).
- Display a visible connection status indicator on the dashboard (green dot = connected, red = disconnected).
- In Node-RED, the WebSocket node handles reconnection automatically for outbound connections; for inbound (browser connects to Node-RED), the browser client must handle reconnect.
- Keep dashboard state client-side (last known values) and mark them as "stale" (e.g., grey color) when disconnected — don't clear the display, which is more confusing.

**Phase to address:** Phase 3 (host integration) — add heartbeat and reconnection before any user testing. Failure mode is highly visible during demos.

---

## Category 4 — Student Project Pitfalls

### Pitfall P1: Scope Creep — Building Everything Before Testing Anything

**What goes wrong:**
The project has many peripherals and two modes. Students commonly wire all peripherals simultaneously, write firmware for all of them at once, and then debug a system where any of 8+ components could be the source of a failure. When something goes wrong (and it will), there is no baseline — every component is a suspect.

**Why it happens:**
- Excitement about the full feature set.
- Underestimating integration complexity.
- Treating "hardware is connected" as equivalent to "hardware works."

**Warning signs:**
- All components wired before any firmware is verified.
- First firmware upload attempts to initialize all libraries simultaneously.
- Project README has no "tested" vs "untested" tracking.

**Prevention:**
- Follow a **strict peripheral-by-peripheral bringup sequence**: one device, one library, one test sketch, confirmed working, then add the next.
- Suggested order: Serial echo → HC-SR04 → IR sensor → LCD → MAX7219 → 7-segment → Buzzer → Buttons → Numpad → All combined.
- Each bringup test should produce observable, unambiguous output (Serial Monitor shows correct readings).
- Only after all peripherals are independently confirmed working should the combined firmware be written.
- Track bringup status in a checklist in the project README or `.planning/`.

**Phase to address:** Phase 1 (hardware bringup) — enforce incremental integration as the phase's definition of done.

---

### Pitfall P2: Late Integration — Hardware and Software Developed in Isolation

**What goes wrong:**
One team member writes all the Arduino firmware. Another writes the Node-RED/Python dashboard. They only try to connect them at 11pm the night before the demo. At this point, the serial protocol defined in firmware does not match what the dashboard parses, timing assumptions differ, and the demo fails in ways that are hard to diagnose under pressure.

**Why it happens:**
- Parallel development seems efficient.
- Serial protocol spec is informal ("we'll figure it out").
- Neither side tests with real hardware / real software until forced to.

**Warning signs:**
- Serial protocol is undocumented ("it's in the code").
- Dashboard is developed using hardcoded test data, never connected to real Arduino.
- First end-to-end test happens less than 48 hours before the deadline.

**Prevention:**
- Write the **serial protocol specification** as a shared document before any code is written. Both firmware and dashboard are implementations of this spec.
- Create a **mock serial emitter** (Python script that sends pre-defined messages at fixed intervals) so dashboard development can proceed independently but against a realistic data stream.
- Schedule a mandatory end-to-end integration test at the end of each development phase, not just at the end.
- Define "Phase X done" as "firmware and dashboard communicate correctly for feature X" — not "firmware done" and "dashboard done" separately.

**Phase to address:** Phase 2 (protocol design) — serial spec is a phase deliverable, not an afterthought.

---

### Pitfall P3: Blocking Code Killing Responsiveness (Structural `delay()` Abuse)

**What goes wrong:**
`delay(1000)` in `loop()` makes the Arduino unresponsive for 1 second to everything — serial input, button presses, sensor reads. Students add `delay()` because it is the simplest way to control timing. As the project grows, delays compound: `delay(100)` for debounce + `delay(500)` for LCD update + `pulseIn()` 30ms timeout = loop iteration can take 630ms. A button press during this window is missed entirely.

**Why it happens:**
- Arduino tutorials use `delay()` everywhere.
- The problem is invisible in simple single-peripheral sketches.
- Only emerges when multiple timing requirements exist simultaneously.

**Warning signs:**
- Button presses are occasionally missed or double-register.
- Serial commands from the dashboard take seconds to execute.
- Game mode has choppy, inconsistent update rates.
- Code has more than 2–3 `delay()` calls outside of initialization.

**Prevention:**
- Adopt the **Blink Without Delay** pattern from day one: use `millis()` timestamps to manage all timing.
  ```cpp
  unsigned long lastSensorRead = 0;
  const unsigned long SENSOR_INTERVAL = 100; // ms

  void loop() {
    if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
      lastSensorRead = millis();
      readSensors();
    }
    // other tasks run every loop iteration, never blocked
    checkButtons();
    readSerial();
    updateGame();
  }
  ```
- Reserve `delay()` only for: initial power-on stabilization (`setup()` only), and one-shot blocking operations where responsiveness does not matter (e.g., a 500ms startup tone).
- For button debounce, use a state machine with `millis()` rather than `delay(50)`.
- Treat any `delay()` call in `loop()` as a code review flag requiring justification.

**Phase to address:** Phase 1 (firmware foundation) — establish `millis()`-based timing as the project coding standard before any feature code.

---

### Pitfall P4: Dual-Mode State Machine Not Designed Upfront

**What goes wrong:**
"Monitoring mode" and "Game mode" seem simple as two `if` branches. In practice, each mode has its own:
- Peripheral update rates
- Serial message format
- Input handling (sensors vs Numpad)
- Display content (LCD shows sensor data vs game score)
- Expected behavior on mode switch (save game state? reset LCD immediately?)

Without an explicit state machine design, the mode-switching code becomes a tangle of flags and special cases. Adding a third state (e.g., "calibration mode" or "game over screen") becomes a refactor rather than an extension.

**Warning signs:**
- Mode switch is implemented as `bool gameMode = false`.
- Mode-specific behavior is scattered across every function with `if (gameMode)` checks.
- Switching modes mid-game crashes or corrupts LCD state.
- Adding any new mode would require changing 10+ places in the code.

**Prevention:**
- Design an explicit state machine with named states before writing any mode-related code:
  ```cpp
  enum SystemState { STATE_MONITOR, STATE_GAME, STATE_GAME_OVER };
  SystemState currentState = STATE_MONITOR;
  ```
- Each state has an `onEnter()` function (initialization), an `update()` function (per-loop logic), and an `onExit()` function (cleanup).
- State transitions are explicit functions: `transitionToGame()` handles LCD clear, MAX7219 init, score reset.
- Serial messages include the current state: `M:0` (monitor) or `M:1` (game) — host dashboard updates its display mode accordingly.

**Phase to address:** Phase 2 (firmware architecture) — design the state machine as a phase deliverable before implementing either mode.

---

## Phase-Specific Warning Summary

| Phase | Topic | Primary Pitfall | Mitigation |
|---|---|---|---|
| Phase 1 | Hardware bringup | Wiring all peripherals before testing any | Incremental per-peripheral bringup checklist |
| Phase 1 | Firmware foundation | `String` class / `delay()` patterns established early | Enforce `char[]` + `millis()` from first commit |
| Phase 1 | Pin planning | Running out of pins mid-project | Complete pin assignment table before wiring |
| Phase 1 | Power | USB current limit exceeded | Test with all peripherals on before writing app code |
| Phase 2 | Serial protocol | Protocol undocumented, mismatched between firmware and host | Write spec document as phase deliverable |
| Phase 2 | HC-SR04 | `pulseIn()` blocking loop | Implement non-blocking pattern via NewPing or `micros()` state machine |
| Phase 2 | I2C | Address conflict between LCD and Numpad | Run `i2c_scanner` as very first firmware test |
| Phase 2 | State machine | Dual-mode implemented as ad-hoc flags | Design `enum SystemState` state machine upfront |
| Phase 3 | Node-RED | Serial node delivers Buffer not String; splits on timeout | Add `.toString().trim()` Function node; configure split on `\n` |
| Phase 3 | Python | `readline()` blocks main thread | Dedicated reader thread + `threading.Queue` |
| Phase 3 | WebSocket | Stale data displayed on disconnect | Heartbeat + reconnection + "stale" visual indicator |
| Phase 3 | Integration | First end-to-end test the night before demo | Mandatory integration test at end of each phase |

---

## Sources and Confidence Notes

- **H1 (SRAM/String):** HIGH confidence. ATmega328P SRAM = 2048 bytes is a hardware fact. `String` heap fragmentation on AVR is a well-documented issue in Arduino community for 10+ years. `F()` macro behavior documented in Arduino reference.
- **H2 (I2C conflict):** HIGH confidence. PCF8574 default address `0x27`/`0x3F` is datasheet-documented. `i2c_scanner` recommendation is standard practice.
- **H3 (Pin budget):** HIGH confidence. Arduino Nano pinout is fixed hardware specification. TM1637 pin count verified against library documentation.
- **H4 (Power):** HIGH confidence. USB 2.0 500mA limit is a spec. MAX7219 current draw documented in Maxim datasheet (up to 320mA for 64 LEDs at full current).
- **H5 (pulseIn):** HIGH confidence. `pulseIn()` is documented as blocking in Arduino reference. NewPing library non-blocking approach is well-established.
- **S1–S4 (Serial):** HIGH confidence. UART buffer size (64 bytes on ATmega328P) is documented in datasheet. Baud rate/framing issues are deterministic hardware behavior.
- **W1 (Node-RED):** MEDIUM confidence. Node-RED serial node behavior based on training knowledge; recommend verifying against `node-red-node-serialport` current documentation.
- **W2 (pyserial):** HIGH confidence. pyserial blocking behavior is documented in pyserial docs. Python GIL and threading behavior is well-established.
- **W3 (WebSocket):** MEDIUM confidence. Reconnection behavior specifics depend on Node-RED version and client implementation. Pattern recommendations are standard web practice.
- **P1–P4 (Student pitfalls):** MEDIUM-HIGH confidence. Based on observed patterns in pedagogical Arduino projects; not empirically studied but widely corroborated in educational computing literature and maker community.
