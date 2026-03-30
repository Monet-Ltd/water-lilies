# Agent-Native IoT Display

> 讓 IoT 設備自我描述，AI agent 讀 `/info` 就能自動學會怎麼使用。
> 同一句 prompt 安裝不同產品，不需要針對每個型號寫不同流程。

## 核心概念

傳統 IoT 需要讀 datasheet、找 SDK、寫驅動。這個做法讓設備在 `/info` endpoint 用自然語言描述自己的能力和 API，agent 直接消費。

```
用戶: "把 Claude Code rate limit 顯示在我的 OLED 上，IP 是 192.168.1.50"

Agent:
  1. GET http://192.168.1.50/info → 讀到自然語言使用手冊
  2. 理解設備能力、API、螢幕規格
  3. 自動配置推送
  4. Done
```

## 通訊架構

### WiFi HTTP 推送（唯一持續推送方式）

設備開 HTTP server，電腦有新資料就 `curl POST` 過去。

```
statusLine command
  → curl POST 到設備 IP
    → 設備收到後更新螢幕
```

### USB Serial（僅限一次性 provisioning）

Serial 用於首次設定（WiFi credentials + auth token）。ESP32-C3 USB-CDC 每次打開 serial port 會 reset 晶片，因此**不能用於持續資料推送**。

使用 CH340/CP2102 外接 USB-UART 的板子不受此限制。

## API Endpoints

| Endpoint | 用途 | 認證 |
|----------|------|------|
| `GET /info` | 自然語言設備文件 | 不需要 |
| `GET /status` | 結構化 JSON 狀態 | 不需要 |
| `POST /display` | Drawing Commands 繪圖 | Bearer token |
| `POST /clear` | 清除螢幕 | Bearer token |
| `POST /wifi` | 設定 WiFi（serial only） | 不需要 |
| `POST /provision` | 產生 auth token（serial only） | 不需要 |
| `POST /factory-reset` | 清除所有儲存（serial only） | 不需要 |

### 認證

- `POST /provision`（僅 serial）產生隨機 16 字元 hex token
- 所有 HTTP POST 需帶 `Authorization: Bearer <token>`
- GET 不需認證（唯讀，無副作用）
- Token 存 NVS，重開機不遺失

### Drawing Commands 格式

```json
{
  "draw": [
    { "type": "text", "x": 0, "y": 13, "text": "Current Session", "size": 10 },
    { "type": "text", "x": 128, "y": 13, "text": "73%", "size": 10, "align": "right" },
    { "type": "bar", "x": 0, "y": 16, "w": 128, "h": 12, "pct": 73 },
    { "type": "rect", "x": 0, "y": 0, "w": 50, "h": 50, "fill": true },
    { "type": "circle", "x": 64, "y": 32, "r": 10, "fill": false },
    { "type": "line", "x1": 0, "y1": 0, "x2": 127, "y2": 63 },
    { "type": "pixel", "x": 10, "y": 10 }
  ]
}
```

- `text` 支援 `"align": "right"`（x 為右邊界，自動往左偏移）
- 單色設備不需要 color 參數
- 彩色設備加 `"color": "#RRGGBB"`

### `/info` — 自然語言文件

設備根據狀態回傳不同內容：

**未連線（Serial 模式）**：引導用戶完成 WiFi 設定和 token provisioning。

**已連線（WiFi 模式）**：完整 API 文件，包含所有 endpoint、drawing types、範例 curl 指令。

### `/status` — 結構化 JSON

```json
{
  "device": "Water Lilies Display MVP",
  "api_version": "0.1.0",
  "screen": { "w": 128, "h": 64, "type": "monochrome" },
  "mode": "wifi",
  "ip": "192.168.1.195",
  "wifi_rssi": -42,
  "uptime_s": 86400,
  "free_heap": 180000,
  "capabilities": ["display", "clear"],
  "endpoints": ["POST /display", "POST /clear"]
}
```

職責分離：
- `/info` — 自然語言，給 agent 理解「怎麼用」
- `/status` — 結構化 JSON，給 agent 程式化讀取「能做什麼」

## Provisioning Flow

```
開機
  ├─ 有儲存的 WiFi credentials？
  │   → 嘗試自動重連（timeout 10s）
  │     → 成功：啟動 HTTP server
  │     → 失敗：Serial 模式，OLED 顯示 "WiFi failed"
  └→ 沒有 credentials
      → Serial 模式，OLED 顯示 "Setup via USB..."
```

### 持久化

WiFi credentials 和 auth token 存在 ESP32 NVS：
- 重開機自動重連，不需重新 provisioning
- `POST /factory-reset`（僅 serial）清除所有儲存

### Serial Protocol

Request：一行，以 `\n` 結尾（JSON body 不可含換行）。
Response：以 `\n---END---\n` 結尾。

```
GET /info\n
POST /wifi {"ssid":"MyNet","pass":"xxx"}\n
POST /provision\n
```

錯誤碼：
- `ERR:SSID_NOT_FOUND` — WiFi 網路找不到
- `ERR:AUTH_FAILED` — WiFi 密碼錯誤
- `ERR:DHCP_TIMEOUT` — 連上 AP 但拿不到 IP（常見於 5GHz SSID）
- `ERR:INVALID_JSON` — JSON 解析失敗
- `ERR:UNAUTHORIZED` — Token 錯誤（HTTP only）

### WiFi 限制

- ESP32-C3 **只支援 2.4GHz WiFi**
- 5GHz SSID 會出現 `ERR:DHCP_TIMEOUT`（不是 `ERR:SSID_NOT_FOUND`）
- 設備和電腦必須在同一個區網

## 設計原則

- **自我描述** — 文件在設備裡，跟韌體綁定，OTA 更新時自動同步
- **Agent-native** — `/info` 用自然語言，消費者是 LLM 不是 parser
- **統一介面** — Serial 和 HTTP 用同樣的指令格式，只是 transport 不同
- **狀態感知** — `/info` 根據當前狀態回傳不同內容，agent 永遠知道下一步該做什麼
- **推送優先** — 電腦推送到設備，不浪費電輪詢
- **安全基本盤** — Provisioning 走 serial（物理接觸），HTTP 需要 token 認證
- **持久化** — WiFi credentials 和 token 存 NVS，重開機自動恢復

## 靈感來源

- Claude Code statusLine 的安裝方式（告訴 agent 怎麼用，它自己配置）
- MCP (Model Context Protocol) 的 capability discovery 精神
- 但套用在實體硬體上
