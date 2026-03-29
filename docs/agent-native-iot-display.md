# Agent-Native IoT Display

> 讓 IoT 設備自我描述，AI agent 讀 `/info` 就能自動學會怎麼使用。
> 同一句 prompt 安裝不同產品，不需要針對每個型號寫不同流程。

## 核心概念

傳統 IoT 需要讀 datasheet、找 SDK、寫驅動。這個做法讓設備在 `/info` endpoint 用自然語言描述自己的能力和 API，agent 直接消費。

```
用戶: "把 Claude Code rate limit 顯示在我的 OLED 上，IP 是 192.168.1.50"

Agent:
  1. GET http://192.168.1.50/info → 讀到完整使用手冊
  2. 理解設備能力、API、螢幕規格
  3. 自動配置推送
  4. Done
```

## 架構

### 推送模式（IoT 開 HTTP server）

不是 Mac 開 server 讓 IoT 輪詢，而是反過來：IoT 開 server，Mac 端有新資料就推。

```
statusLine command
  → curl POST 到 ESP32 IP
    → ESP32 收到後更新 OLED
```

好處：
- 即時更新，不浪費電
- ESP32 邏輯簡單 — 收到什麼顯示什麼
- Mac 端不用額外跑 server

### API Endpoints

| Endpoint | 用途 | Payload |
|----------|------|---------|
| `POST /display` | Drawing Commands（結構化繪圖） | ~200B JSON |
| `POST /framebuffer` | Raw RGB565 pixels（完全自由度） | ~32KB |
| `POST /clear` | 清除螢幕 | 無 |
| `POST /brightness` | 調整亮度 | `{"level": 0-255}` |
| `POST /wifi` | 設定/變更 WiFi | `{"ssid":"...","pass":"..."}` |
| `GET /info` | 完整自然語言設備文件（setup 用） | 無 |
| `GET /status` | 輕量狀態查詢（省 token） | 無 |

所有 HTTP `POST` 請求需帶 `Authorization: Bearer <token>` header。
Token 在 provisioning 階段透過 serial 產生（見下方）。

### 認證機制

Provisioning 時設備產生一組隨機 token，透過 serial 回傳給 agent。之後所有 HTTP POST 請求必須帶此 token。

```
Serial:  POST /provision
Device:  TOKEN a1b2c3d4e5f6

之後 HTTP:
  curl -X POST http://IP/display \
    -H "Authorization: Bearer a1b2c3d4e5f6" \
    -H "Content-Type: application/json" \
    -d '{...}'
```

- Token 存在 NVS，重開機不遺失
- `POST /provision` 可重新產生 token（僅限 serial，HTTP 無法呼叫）
- `GET /info` 和 `GET /status` 不需要認證（唯讀，無副作用）

### Drawing Commands 格式

```json
{
  "bg": "#000000",
  "draw": [
    { "type": "text", "x": 0, "y": 14, "text": "5h 73%", "color": "#00FF00", "size": 16 },
    { "type": "bar", "x": 0, "y": 20, "w": 128, "h": 8, "pct": 75, "fg": "#00FF00", "bg": "#333333" },
    { "type": "rect", "x": 0, "y": 0, "w": 50, "h": 50, "color": "#FF0000", "fill": true },
    { "type": "circle", "x": 64, "y": 64, "r": 20, "color": "#0088FF", "fill": false },
    { "type": "line", "x1": 0, "y1": 0, "x2": 127, "y2": 127, "color": "#FFAA00" },
    { "type": "pixel", "x": 10, "y": 10, "color": "#FFFFFF" },
    { "type": "image", "x": 96, "y": 0, "w": 32, "h": 32, "data": "<base64 RGB565>" }
  ]
}
```

Supported types: `text`, `rect`, `bar`, `circle`, `line`, `pixel`, `image`

### Framebuffer 格式

Raw RGB565 pixels，**big-endian**。每個 pixel 2 bytes：

```
Byte layout per pixel:
  [high byte]           [low byte]
  RRRRRGGG              GGGBBBBB

Example: pure red (#FF0000)
  R=31, G=0, B=0 → 0xF800 → bytes: 0xF8, 0x00

Total: width x height x 2 bytes (128x128 = 32768 bytes)
```

### `/status` — 輕量狀態查詢

Agent 日常使用時呼叫 `/status` 而非 `/info`，省 token：

```json
{
  "device": "Pixie Display v1.2",
  "screen": { "w": 128, "h": 128, "type": "color" },
  "mode": "wifi",
  "ip": "192.168.1.50",
  "wifi_rssi": -42,
  "uptime_s": 86400
}
```

## Provisioning Flow（開箱設定）

問題：新設備沒有 WiFi 資訊，無法無線連接。

解法：開機預設 USB Serial 模式。連上 WiFi 後 serial 仍然可用作 fallback。

### Boot 流程

```
開機
  ├→ 有儲存的 WiFi credentials？
  │   → 嘗試自動重連（timeout 10s）
  │     → 成功：啟動 HTTP server + serial 並存
  │     → 失敗：僅 serial 模式，OLED 顯示 "WiFi failed, serial ready"
  └→ 沒有 credentials
      → 僅 serial 模式，OLED 顯示 "Waiting for setup via USB..."

  Serial 永遠可用，不論 WiFi 狀態。
```

### 持久化

WiFi credentials 和 auth token 存在 ESP32 NVS (Non-Volatile Storage)：
- 重開機自動重連，不需要重新 provisioning
- WiFi 斷線後自動重試，重試失敗仍可透過 serial 操作
- `POST /factory-reset`（僅 serial）清除所有儲存

### Serial Protocol

Serial 和 HTTP 用相同的 command pattern，但需要 framing 來處理 message 邊界。

**Request**（一行，以 `\n` 結尾）：

```
GET /info\n
POST /wifi {"ssid":"MyNet","pass":"xxx"}\n
POST /display {"bg":"#000","draw":[...]}\n
```

注意：JSON body 不可包含換行，必須是單行。

**Response**（以 `\n---END---\n` 結尾）：

```
OK 192.168.1.50
---END---
```

```
# Pixie Display v1.2
## 128x128 Color OLED ...
(完整 /info 內容)
---END---
```

**Error response**（結構化錯誤碼）：

```
ERR:AUTH_FAILED WiFi password rejected
---END---
```

錯誤碼：
- `ERR:SSID_NOT_FOUND` — 找不到指定的 WiFi 網路
- `ERR:AUTH_FAILED` — WiFi 密碼錯誤
- `ERR:DHCP_TIMEOUT` — 連上 AP 但拿不到 IP
- `ERR:INVALID_JSON` — JSON 解析失敗
- `ERR:UNAUTHORIZED` — Token 錯誤（HTTP only）
- `ERR:UNKNOWN_CMD` — 不認識的指令

### Serial-only 指令

這些指令只能透過 serial 執行，HTTP 無法呼叫：

| 指令 | 用途 |
|------|------|
| `POST /provision` | 產生新 auth token |
| `POST /factory-reset` | 清除所有儲存（WiFi、token） |

### `/info` 根據狀態回傳不同內容

**Serial 模式（未連線）：**

```
# Pixie Display v1.2
## 128x128 Color OLED (ST7789, RGB565)
## Current Mode: USB Serial (115200 baud)

This device is not yet connected to WiFi.

## Quick Start
  1. POST /wifi {"ssid":"YourNetwork","pass":"YourPassword"}
     → Returns "OK 192.168.x.x" on success
     → Error codes: ERR:SSID_NOT_FOUND, ERR:AUTH_FAILED, ERR:DHCP_TIMEOUT
  2. POST /provision
     → Returns "TOKEN <token>" — save this for HTTP authentication

## Available Commands (Serial)
  GET /info              → This help text
  GET /status            → Device status (JSON)
  POST /wifi {...}       → Connect to WiFi
  POST /provision        → Generate auth token
  POST /display {...}    → Draw (same JSON format as HTTP mode)
  POST /clear            → Clear screen
  POST /factory-reset    → Wipe all stored credentials

## Serial Protocol
  Request: one line ending with \n (no newlines in JSON body)
  Response: ends with \n---END---\n

After WiFi is connected, HTTP server starts. Serial remains available.
```

**WiFi 模式（已連線）：**

```
# Pixie Display v1.2
## 128x128 Color OLED (ST7789, RGB565)
## Current Mode: WiFi HTTP + Serial
## IP: 192.168.1.50

## Authentication
All POST requests require: Authorization: Bearer <token>
Token is generated via serial: POST /provision
GET requests (/info, /status) do not require auth.

## Endpoints
  GET  /info        → This help text (full, for setup)
  GET  /status      → Device status JSON (lightweight, for polling)
  POST /display     → Draw shapes and text (JSON, max 4KB)
  POST /framebuffer → Raw RGB565 pixels (big-endian, width*height*2 bytes)
  POST /clear       → Clear screen
  POST /brightness  → Set brightness {"level": 0-255}
  POST /wifi        → Change WiFi {"ssid":"...","pass":"..."}

## Drawing Types
  text:   { "type":"text", "x":0, "y":14, "text":"hello", "color":"#FFF", "size":12 }
  rect:   { "type":"rect", "x":0, "y":0, "w":50, "h":50, "color":"#F00", "fill":true }
  bar:    { "type":"bar", "x":0, "y":20, "w":128, "h":8, "pct":75, "fg":"#0F0", "bg":"#333" }
  circle: { "type":"circle", "x":64, "y":64, "r":20, "color":"#08F", "fill":false }
  line:   { "type":"line", "x1":0, "y1":0, "x2":127, "y2":127, "color":"#FA0" }
  pixel:  { "type":"pixel", "x":10, "y":10, "color":"#FFF" }
  image:  { "type":"image", "x":0, "y":0, "w":32, "h":32, "data":"<base64 RGB565>" }

Supported text sizes: 8, 12, 16, 24, 32
Colors: hex "#RRGGBB" format

## Framebuffer Format
  RGB565, big-endian, 2 bytes per pixel.
  Byte layout: [RRRRRGGG] [GGGBBBBB]
  Example red (#FF0000): 0xF8 0x00
  Total size: 128 * 128 * 2 = 32768 bytes

## Example: Claude Code statusLine
  curl -s -m 1 -X POST "http://THIS_IP/display" \
    -H "Authorization: Bearer YOUR_TOKEN" \
    -H "Content-Type: application/json" \
    -d '{"bg":"#000","draw":[{"type":"text","x":0,"y":14,"text":"5h 73%","color":"#0F0","size":16}]}'
```

## Agent Setup 體驗

### 用戶說：

```
我剛買了一個 Pixie Display，USB 接在 /dev/tty.usbserial-XXX，請幫我設定。
```

### Agent 自動：

1. 開 serial port (115200 baud)
2. `GET /info` → 讀到 Serial 模式文件，了解設備能力
3. 問用戶 WiFi SSID 和密碼
4. `POST /wifi {"ssid":"Home","pass":"xxx"}` → 收到 `OK 192.168.1.50`
5. `POST /provision` → 收到 `TOKEN a1b2c3d4e5f6`，記下來
6. `GET http://192.168.1.50/info` → 確認 WiFi 模式正常
7. 問用戶要顯示什麼，自動配置推送（帶上 auth token）

### 不同產品，同一句 prompt：

```
Pixie Display 128x128  → /info 回傳 color OLED API
Pixie Eink 296x128     → /info 回傳 e-ink API（沒有 color，有 partial refresh）
Pixie LED Strip 60     → /info 回傳 LED 控制 API
Pixie Speaker          → /info 回傳音效播放 API
```

## 設計原則

- **自我描述** — 文件在設備裡，跟韌體綁定，OTA 更新時自動同步
- **Agent-native** — `/info` 用自然語言，消費者是 LLM 不是 parser
- **統一介面** — Serial 和 HTTP 用同樣的指令格式，只是 transport 不同
- **狀態感知** — `/info` 根據當前狀態回傳不同內容，agent 永遠知道下一步該做什麼
- **推送優先** — IoT 開 server，有新資料就推，不浪費電輪詢
- **雙模並存** — WiFi 連上後 serial 仍然可用，WiFi 斷線不會變磚
- **安全基本盤** — Provisioning 走 serial（物理接觸），HTTP 需要 token 認證
- **持久化** — WiFi credentials 和 token 存 NVS，重開機自動恢復

## 靈感來源

- Claude Code statusLine 的安裝方式（告訴 agent 怎麼用，它自己配置）
- MCP (Model Context Protocol) 的 capability discovery 精神
- 但套用在實體硬體上

## Open Questions

- [ ] OTA 更新機制（endpoint? 檔案格式? 驗證?）
- [ ] 多 source 同時推送的行為定義（last-writer-wins? area lock? z-index?）
- [ ] mDNS discovery（`pixie-display.local` 免記 IP）
- [ ] 省電模式（OLED 長時間不更新時降亮度或關閉）
