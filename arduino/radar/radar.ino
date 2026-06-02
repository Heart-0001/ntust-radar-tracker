// radar.ino
// HC-SR04 + SG90 掃描追蹤雷達
// 4x4 LED 矩陣（共陰）以 Timer2 interrupt 刷新
// Serial 輸出 JSON → Node-RED
//
// 需要安裝的函式庫：LiquidCrystal_I2C

#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ── 接腳定義 ──────────────────────────────────────────────
#define ROW0       2
#define ROW1       3
#define ROW2       4
#define ROW3       5
#define TRIG       7
#define ECHO       8
#define SERVO_PIN  9
#define COL0      A0
#define COL1      A1
#define COL2      A2
#define COL3      A3

// ── 可調參數 ──────────────────────────────────────────────
#define LCD_ADDR     0x27  // LCD I2C 位址，若畫面空白改試 0x3F
#define DETECT_CM    100   // 小於此距離視為偵測到物體（公分），對齊 Node-RED 顯示範圍
#define STEP_DEG     2     // 每步轉幾度
#define STEP_MS      50    // 每步間隔（ms），數字越大轉越慢
#define LCD_MS       200   // LCD 更新間隔（ms）
#define LOST_STEPS   15    // 連續幾步沒偵測到 → 回掃描模式

// ── 物件 ─────────────────────────────────────────────────
Servo servo;
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// ── 4x4 矩陣（共陰）─────────────────────────────────────
// matrixBuf[row] 的每個 bit 對應該行哪顆 LED 要亮
volatile uint8_t matrixBuf[4] = {};
volatile uint8_t curRow = 0;
const uint8_t ROW_PINS[4] = {ROW0, ROW1, ROW2, ROW3};
const uint8_t COL_PINS[4] = {COL0, COL1, COL2, COL3};

// Timer2 ISR：1ms 觸發一次，輪流掃描 4 行
ISR(TIMER2_COMPA_vect) {
    for (uint8_t i = 0; i < 4; i++) digitalWrite(ROW_PINS[i], LOW);
    uint8_t mask = matrixBuf[curRow];
    for (uint8_t c = 0; c < 4; c++)
        digitalWrite(COL_PINS[c], (mask >> c) & 1 ? LOW : HIGH);
    digitalWrite(ROW_PINS[curRow], HIGH);
    curRow = (curRow + 1) & 3;
}

void setupTimer2() {
    TCCR2A = (1 << WGM21);   // CTC mode
    TCCR2B = (1 << CS22);    // prescaler 64 → 1ms @ 16MHz
    OCR2A  = 249;
    TIMSK2 = (1 << OCIE2A);
    sei();
}

// ── 系統狀態 ──────────────────────────────────────────────
int  angle      = 0;
int  dir        = 1;
int  lastDist   = 999;
bool tracking   = false;
bool prevDetect = false;
int  lastAngle  = 90;
int  lostCnt    = 0;

unsigned long lastStepAt = 0;
unsigned long lastLCDAt  = 0;

// ── HC-SR04 量距離 ────────────────────────────────────────
// 單次量測（µs→cm，999=沒回音/超出範圍）
int pingOnce() {
    digitalWrite(TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);
    long t = pulseIn(ECHO, HIGH, 7000UL);   // 7ms timeout ≈ 120cm（配合 100cm 偵測，留點餘裕）
    return t ? (int)(t * 0.034f / 2) : 999;
}

// 量 3 次，先丟掉 999 爆掉值，只對「有效讀數」取中位數
// 三次都爆才回 999（真的沒東西）—— 避免馬達干擾造成的單次爆值蓋掉真實距離
int measureDist() {
    int v[3];
    uint8_t n = 0;
    for (uint8_t i = 0; i < 3; i++) {
        int d = pingOnce();
        if (d < 999) v[n++] = d;    // 只收有效讀數
        delay(4);                   // 間隔 4ms，讓上一次回音消散
    }
    if (n == 0) return 999;         // 三次都沒回音
    if (n == 1) return v[0];
    if (n == 2) return (v[0] + v[1]) / 2;
    int hi = max(v[0], max(v[1], v[2]));   // n == 3：取中位數
    int lo = min(v[0], min(v[1], v[2]));
    return v[0] + v[1] + v[2] - hi - lo;
}

// ── 更新矩陣：角度→欄，距離→行 ─────────────────────────
// 欄 0-3 = 角度區 0-45° / 45-90° / 90-135° / 135-180°
// 行 0-3 = 距離區 <25cm / <50 / <75 / ≥75cm（對齊 100cm 偵測範圍）
void setMatrix(int ang, int dist, bool det) {
    for (uint8_t i = 0; i < 4; i++) matrixBuf[i] = 0;
    if (!det || dist >= 100) return;
    uint8_t col = (uint8_t)map(ang, 0, 180, 0, 3);
    uint8_t row = (dist < 25) ? 0 : (dist < 50) ? 1 : (dist < 75) ? 2 : 3;
    matrixBuf[row] = (1 << col);
}

// ── Serial JSON 輸出 ──────────────────────────────────────
void sendJSON(int ang, int dist, bool det) {
    Serial.print(F("{\"angle\":"));    Serial.print(ang);
    Serial.print(F(",\"dist\":"));     Serial.print(min(dist, 999));
    Serial.print(F(",\"detected\":")); Serial.print(det ? F("true") : F("false"));
    Serial.print(F(",\"mode\":\""));   Serial.print(tracking ? F("track") : F("scan"));
    Serial.println(F("\"}"));
}

// ── LCD 顯示 ──────────────────────────────────────────────
void updateLCD() {
    lcd.setCursor(0, 0);
    lcd.print(tracking ? F("Mode: TRACK     ") : F("Mode: SCAN      "));
    lcd.setCursor(0, 1);
    uint8_t n = 0;
    n += lcd.print(F("A:"));
    n += lcd.print(angle);
    n += lcd.print(F(" D:"));
    if (lastDist < 999) {
        n += lcd.print(lastDist);
        n += lcd.print(F("cm"));
    } else {
        n += lcd.print(F("---cm"));
    }
    while (n < 16) { lcd.print(' '); n++; }   // 補空白到 16 格，蓋掉上一次的殘留字元
}

// ── Setup ─────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    for (uint8_t i = 0; i < 4; i++) {
        pinMode(ROW_PINS[i], OUTPUT); digitalWrite(ROW_PINS[i], LOW);
        pinMode(COL_PINS[i], OUTPUT); digitalWrite(COL_PINS[i], HIGH);
    }
    pinMode(TRIG,   OUTPUT);
    pinMode(ECHO,   INPUT);

    servo.attach(SERVO_PIN);
    servo.write(0);

    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0); lcd.print(F("Radar Ready"));
    lcd.setCursor(0, 1); lcd.print(F("Scanning..."));

    setupTimer2();
    delay(500);
}

// ── Loop ──────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    if (now - lastStepAt >= STEP_MS) {
        lastStepAt = now;

        // 移動伺服馬達
        angle += dir * STEP_DEG;
        bool hitLimit = false;
        if (angle >= 180) { angle = 180; dir = -1; hitLimit = true; }
        if (angle <= 0)   { angle = 0;   dir =  1; hitLimit = true; }
        servo.write(angle);
        delay(15);   // 等馬達轉到定位、電流穩定再量，避免轉動瞬間的電流干擾

        // 量測距離
        int dist   = measureDist();
        lastDist   = dist;
        bool detect = (dist < DETECT_CM);

        // 追蹤邏輯
        if (detect) {
            lostCnt    = 0;
            lastAngle  = angle;
            tracking   = true;
            prevDetect = true;
        } else {
            if (prevDetect && !hitLimit) {
                // 剛失去物體 → 反向
                dir        = -dir;
                prevDetect = false;
            } else if (!detect) {
                prevDetect = false;
            }
            if (++lostCnt > LOST_STEPS) {
                tracking = false;
                lostCnt  = 0;
            }
        }

        setMatrix(detect ? lastAngle : angle, dist, detect);
        sendJSON(angle, dist, detect);
    }

    if (now - lastLCDAt >= LCD_MS) {
        lastLCDAt = now;
        updateLCD();
    }
}
