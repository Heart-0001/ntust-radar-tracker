# Feature Landscape

**Domain:** Arduino Nano dual-mode IoT system (environmental monitoring + interactive game)
**Researched:** 2026-05-19
**Confidence note:** WebSearch and Bash tools unavailable in this session. All findings are based on established practices in embedded IoT, serial communication, and constrained-hardware game design — domains that are stable and well-documented as of the August 2025 knowledge cutoff. Confidence: MEDIUM-HIGH for this project scope.

---

## Table Stakes

Features that evaluators (graders, demo audiences) will expect. Absence signals incompleteness.

### Data Visualization

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Live numeric readout of sensor values | Most basic IoT output; absence is a fail | Low | Distance in cm/mm, IR on/off state |
| Real-time line/area chart for distance over time | Standard IoT dashboard pattern; graders expect trend, not just snapshot | Low-Medium | Node-RED `ui_chart` or Chart.js; rolling 60-second window is sufficient |
| Status indicator (color badge / LED icon) for IR sensor | Binary sensor data best shown as a visual state, not a number | Low | Green = clear, Red = detected |
| Current mode display (Monitoring vs Game) | Bidirectional system must always show what state the Arduino is in | Low | Plain text label is fine |
| Timestamp on last received packet | Shows the connection is alive; absence makes demo look broken | Low | "Last update: 0.3s ago" |

### Serial Communication Reliability

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| Structured message format (JSON or CSV) | Ad-hoc strings break on edge cases; graders will probe this | Low | JSON preferred: `{"mode":"monitor","dist":42,"ir":0}` |
| Start/end delimiters or newline framing | Raw bytes get corrupted; newline (`\n`) terminated lines are the Arduino standard | Low | `Serial.println()` on Arduino + `readline()` on host |
| Graceful serial disconnect handling | Demo machines sometimes drop COM ports; UI must not crash | Low-Medium | Show "Disconnected" banner; auto-reconnect attempt |
| Baud rate consistency (115200 recommended) | 9600 causes visible lag for 10Hz sensor updates; 115200 is standard for Nano | Low | Set both sides identically |
| Dropped-packet detection | Sensor data gaps should not silently corrupt charts | Low | Sequence number or timestamp comparison |

### Game Mode (8x8 LED Matrix + Numpad)

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| A complete, playable game loop | Start screen → play → game over → score → restart | Medium | Without this, it is just a demo, not a game |
| Score display during and after game | Core feedback loop; shows 7-segment integration | Low | Local on 7-seg + sent to web dashboard |
| Responsive numpad input (no debounce lag) | Laggy controls break the game demo immediately | Low-Medium | Hardware debounce + 20ms software debounce |
| Game-over notification to web dashboard | Closes the bidirectional loop for the game mode | Low | `{"event":"gameover","score":15}` on Serial |
| Mode switch preserved correctly | Switching from game back to monitor must resume clean sensor stream | Medium | State machine on Arduino; flush Serial buffer on transition |

---

## Differentiators

Features that would make this stand out in a semester-project context. Not required, but impressive.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Web-triggered game start / reset | Demonstrates true bidirectional control beyond just "send a command" | Low | Button on dashboard sends `CMD:GAME_START\n`; Arduino enters game loop |
| High-score leaderboard on web (session memory) | Graders love seeing data persist across rounds — shows full stack thinking | Low-Medium | In-memory JS array on host side; no DB needed |
| Distance-based game mechanic | HC-SR04 distance controls game speed or difficulty — fuses both modes creatively | Medium | Unique hardware integration; memorable demo moment |
| Animated 8x8 splash screen on mode switch | Scroll text or icon animation via MAX7219 on mode entry | Low-Medium | LedControl library supports this natively |
| IR sensor as game trigger | IR beam-break as an in-game event (e.g., "catch the beam") | Medium | Reuses existing hardware in an unexpected way |
| Serial data export (download CSV) | One-click download of session sensor data from web UI | Low | Pure frontend; `Blob` API in JS; impressive to non-technical evaluators |
| Visual serial health indicator on dashboard | Packet rate meter (e.g., "12 packets/sec") shows system is working | Low | Rolling counter in JS; very low effort, high visual impact |

---

## Anti-Features

Things that are tempting but should be deliberately excluded from this scope.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| Database persistence (SQLite, InfluxDB, etc.) | Adds setup complexity, a new failure mode, and scope creep with near-zero demo value | Store last N readings in memory (circular buffer); already listed as Out of Scope in PROJECT.md |
| WiFi / MQTT / WebSockets from Arduino | Arduino Nano has no WiFi; adding an ESP8266 is a different project | Commit to USB Serial; it is faster to develop and more reliable for a wired demo |
| Multi-player or networked game | Requires networking stack; completely out of scope | Single-player game with score reporting is compelling enough |
| OTA firmware update | Advanced embedded topic; weeks of effort for zero demo value | Flash via USB cable as normal |
| Machine learning / anomaly detection on sensor data | Buzzword feature with no grounding in this hardware scope | Threshold-based alerts (distance < 10cm = warning) achieve the same demo effect |
| Full authentication / user login on web UI | This is a local USB-connected dashboard; auth adds friction, not value | No auth; single-page localhost app is correct |
| Multiple simultaneous games | Hard to test, spreads implementation effort thin | Implement one game excellently rather than two games poorly |
| Touchscreen or mobile-responsive game controls | The game is played on physical numpad; web is observer/score screen | Web dashboard shows score and allows mode-switch; numpad is the controller |
| Persistent game save state | EEPROM on Nano is 1KB; save state for a student game is overkill | High score in SRAM; lost on power-cycle is acceptable |

---

## Game Mode Analysis

**Hardware constraints that drive all decisions:**
- Display: 8x8 pixels (64 LEDs, MAX7219 driver) — monochrome, no greyscale in typical wiring
- Input: Numpad (4x4 matrix, keys 0-9 + # * A B C D) + physical buttons
- CPU: ATmega328P at 16 MHz, 2KB SRAM — game state must fit in ~500 bytes after other allocations
- No display buffer beyond what MAX7219 holds — frame updates are whole-matrix writes

### Snake

| Criterion | Rating | Notes |
|-----------|--------|-------|
| Implementation complexity | Medium | Snake body as linked list or ring buffer; direction from numpad (2/4/6/8 or WASD mapping); collision detection straightforward |
| Fun factor | High | Classic, universally understood, satisfying on 8x8 |
| Hardware fit | Excellent | 8x8 is the canonical Snake grid size; numpad direction keys map naturally |
| SRAM cost | Low | Snake body ≤ 64 cells; 64 bytes for body array; fits easily |
| Recommended | YES — primary game candidate | Lowest risk, highest payoff |

### Tetris (Simplified)

| Criterion | Rating | Notes |
|-----------|--------|-------|
| Implementation complexity | High | 7 tetromino shapes × 4 rotations = 28 bitmasks; line-clear logic; gravity timer; piece collision against settled blocks — all in 2KB SRAM |
| Fun factor | High | Universally known but 8-column width is cramped; pieces feel rushed |
| Hardware fit | Poor-Medium | Standard Tetris board is 10 wide × 20 tall; 8x8 forces non-standard rules and cramped play area; rotations look odd |
| SRAM cost | Medium | Board state = 8 bytes (8 rows × 8 bits); manageable; complexity is in logic, not memory |
| Recommended | NO for primary — consider only if Snake is already done | Effort is disproportionate to the constrained display payoff |

### Reaction / Timing Game

| Criterion | Rating | Notes |
|-----------|--------|-------|
| Implementation complexity | Low | Random LED lights up; player presses corresponding numpad key; measure reaction time in ms; score based on speed/accuracy |
| Fun factor | Medium | Simple but shallow; good for a sub-game or second mode |
| Hardware fit | Excellent | Numpad keys map 1:1 to visual grid positions; buzzer feedback on hit/miss is natural |
| SRAM cost | Minimal | Essentially stateless beyond current target and score |
| Recommended | YES — as secondary game or fallback if Snake is too complex | Very fast to implement (~2 hours); works as a reliable demo if primary game has issues |

### Space Invaders (Simplified)

| Criterion | Rating | Notes |
|-----------|--------|-------|
| Implementation complexity | High | Multiple alien entities with movement pattern; player ship; bullets (player + alien); collision detection per-entity — all must fit in 2KB |
| Fun factor | High | Iconic; but 8x8 means max ~4 aliens and a 1-pixel ship; visually sparse |
| Hardware fit | Poor | 8 columns × 8 rows gives ~2 rows for aliens, 1 row buffer, 1 row player; playfield is too small to feel like Space Invaders |
| SRAM cost | Medium-High | Each alien = x,y,alive byte; 4 aliens = 12 bytes; bullets array; manageable but tight with other game state |
| Recommended | NO — visual payoff does not justify complexity on 8x8 | Reserve as stretch goal only |

### Summary Recommendation

**Implement Snake as the primary game.** It is the canonical 8x8 game, naturally maps to numpad directional input, fits comfortably in 2KB SRAM, and has well-understood implementation patterns. Add a Reaction game as a secondary mode or fallback — it shares the same hardware and can be implemented in an afternoon as insurance.

Tetris and Space Invaders both suffer from the 8x8 constraint in different ways (aspect ratio for Tetris, entity density for Space Invaders) and should not be primary choices.

---

## Feature Dependencies

```
Serial protocol defined
  → All dashboard visualizations (charts, status badges, timestamps)
  → Game score reporting to web
  → Bidirectional command handling

Mode state machine (Arduino)
  → Clean sensor stream in monitor mode
  → Game loop entry/exit
  → LCD + 7-seg update logic

Game loop (Snake recommended)
  → Score display on 7-segment
  → Game-over event to Serial
  → High score tracking on web
```

---

## MVP Recommendation

Prioritize these features to hit a demonstrable, grade-worthy state:

1. Structured Serial protocol (JSON, newline-framed, 115200 baud)
2. Live distance chart + IR status badge on web dashboard
3. Bidirectional: at least one command (mode switch or buzzer trigger) from web to Arduino
4. Snake game — complete loop: start, play, game over, score on 7-segment
5. Game score sent to web dashboard on game over
6. LCD showing current mode and key value

Defer (nice-to-have, not MVP):
- High-score leaderboard (add after game loop is solid)
- Distance-controlled game speed (add after basic game works)
- CSV export (last feature; purely additive)
- Reaction game (only if Snake hits a blocker)

---

## Sources

- PROJECT.md hardware and scope constraints (validated, HIGH confidence)
- Arduino ATmega328P datasheet: 32KB Flash, 2KB SRAM, 1KB EEPROM (HIGH confidence — stable spec)
- MAX7219 LedControl library behavior: full 8x8 bitmap write per frame (HIGH confidence — library is stable and well-documented)
- Node-RED dashboard (node-red-dashboard) widget catalog: ui_chart, ui_gauge, ui_text, ui_button (HIGH confidence — stable as of knowledge cutoff)
- Snake-on-8x8 as canonical implementation: widely taught in embedded courses, fits hardware exactly (HIGH confidence)
- SRAM budget analysis: 2KB total; ~500 bytes for stack/libs leaves ~1.5KB for game state (MEDIUM confidence — depends on library footprint; verify with actual compilation)
- Tetris/Space Invaders 8x8 fit assessment: based on pixel geometry analysis, not specific implementations (MEDIUM confidence)
