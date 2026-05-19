# Arduino Nano 雙模式 IoT 互動系統

## What This Is

以 Arduino Nano 為核心的雙模式系統：平常作為環境監測站，收集超音波距離與紅外線等感測器資料，透過 USB Serial 傳至電腦端的 Node-RED 或 Python 網頁，即時呈現數據並支援遠端控制；同時可切換至互動遊戲模式，以 8x8 LED 矩陣、Numpad 與按鈕進行遊戲操作。
本專案為 NTUST 微電腦應用實驗室 114-2 學期期末作品。

## Core Value

從網頁端能即時看到 Arduino 的感測器資料，並能送指令回去控制硬體。

## Requirements

### Validated

(None yet — ship to validate)

### Active

**感測器與資料收集**
- [ ] 超音波感測器（HC-SR04）持續量測距離並輸出至 Serial
- [ ] 紅外線感測器偵測物體存在/觸發事件
- [ ] 所有感測器資料以固定格式透過 USB Serial 傳送至電腦

**本地顯示**
- [ ] I2C LCD 顯示目前模式與關鍵數值
- [ ] 七段顯示器顯示數字資料（距離、計數等）
- [ ] 8x8 LED 矩陣顯示狀態圖示或遊戲畫面
- [ ] LED 燈作為狀態指示（正常/警告/遊戲）
- [ ] 蜂鳴器在事件觸發或遊戲動作時發出音效

**互動輸入**
- [ ] 按鈕切換監測模式與遊戲模式
- [ ] Numpad（數字鍵盤）在遊戲模式下作為操作輸入

**網頁端（Node-RED 或 Python+HTML）**
- [ ] 即時接收並顯示 Arduino 傳來的感測器數據（圖表/數值）
- [ ] 提供介面讓使用者送控制指令回 Arduino（如：切換模式、觸發蜂鳴器）
- [ ] 歷史資料記錄（可選）

**遊戲模式**
- [ ] 至少一種可玩的遊戲（種類待定，以 8x8 矩陣為主畫面）
- [ ] 遊戲分數或結果可傳送至網頁端記錄

### Out of Scope

- SG90 伺服馬達 — 使用者決定此次不使用
- WiFi 無線傳輸 — 改用 USB Serial，不需額外模組
- 資料庫持久化儲存 — 超出學期專案範圍，歷史記錄為可選功能
- 多人連線 — 超出範圍

## Context

- **平台**：Arduino Nano（ATmega328P，無內建 WiFi/BT）
- **連線方式**：USB Serial（9600 或 115200 baud）→ 電腦
- **網頁技術**：Node-RED（首選，快速）或 Python Flask/FastAPI + HTML/JS
- **硬體清單**：
  - HC-SR04 超音波感測器
  - I2C LCD 模組
  - 紅外線感測器（接收模組）
  - 數字 Numpad（矩陣掃描或 I2C）
  - 8x8 LED 矩陣（MAX7219 驅動）
  - LED × 多顆
  - 按鈕 × 多個
  - 蜂鳴器（主動或被動）
  - 七段顯示器（共陰/共陽或 TM1637）
- **I2C 裝置**：LCD + 可能的 Numpad 共用 I2C 匯流排，需注意位址衝突
- **Serial 通訊格式**：待定，建議 JSON 或 CSV 格式

## Constraints

- **Hardware**: Arduino Nano — 限 ATmega328P，Flash 32KB / SRAM 2KB，Pin 數有限
- **Connectivity**: USB Serial 線接電腦，網頁服務必須在同一台電腦上運行
- **Timeline**: NTUST 114-2 學期期末截止
- **Scope**: 學期專案，功能以可展示、可操作為優先

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| USB Serial 而非 WiFi | 不需額外模組，Nano 原生支援，開發最快 | — Pending |
| 雙模式設計（監測 + 遊戲） | 同時展示感測器整合與互動介面能力 | — Pending |
| 遊戲種類待定 | 先建立框架，遊戲內容後續決定 | — Pending |

---

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

---
*Last updated: 2026-05-19 after initialization*
