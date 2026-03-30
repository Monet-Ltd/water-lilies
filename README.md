# Water Lilies

> **Monet Ltd.** — Agent-native IoT display that describes itself.

Water Lilies 是一系列 IoT 顯示設備。每台設備在 `/info` endpoint 用自然語言描述自己的能力，
AI agent 讀一次就知道怎麼用。同一句 prompt 適配所有硬體型號，不需要針對每個產品寫驅動或文件。

```
用戶: /monet-setup

Agent:
  1. 偵測 USB serial → 讀 /info → 了解設備
  2. 詢問 WiFi credentials → 連線
  3. 產生 auth token → 儲存設定
  4. 注入 statusLine 推送 → 螢幕自動同步 rate limit
  5. Done
```

## 架構

```
┌─────────────┐      USB Serial        ┌──────────────────┐
│  電腦 / Agent │◄── (一次性 setup) ───►│  Water Lilies     │
│             │                        │  IoT Device       │
│  curl POST  │───── WiFi HTTP ───────►│                   │
│  (推送資料)  │                        │  GET /info   (自述)│
│             │◄──── WiFi HTTP ────────│  GET /status (狀態)│
│             │                        │  POST /display    │
│             │                        │  POST /clear      │
└─────────────┘                        └──────────────────┘
```

- **Serial** 僅用於一次性 provisioning（WiFi + token）
- **WiFi HTTP** 用於持續的資料推送
- ESP32-C3 USB-CDC 每次開 serial port 會 reset 晶片，因此 serial 不能用於持續推送

## 快速開始

### 安裝 Claude Code plugin

```bash
claude plugin add Monet-Ltd/monet-setup
```

### 設定設備

```bash
/monet-setup
```

Agent 會自動：偵測 serial port → 讀設備資訊 → 詢問 WiFi → 連線 → 產生 token → 注入 statusLine → 完成。

## 設備 API

### `GET /info` — 自然語言使用手冊

設備用 plain text 描述自己的所有能力。消費者是 LLM，不是 parser。
內容根據設備狀態動態變化（未連線 vs 已連線顯示不同指引）。

### `GET /status` — 結構化狀態

```json
{
  "device": "Water Lilies Display MVP",
  "api_version": "0.1.0",
  "screen": { "w": 128, "h": 64, "type": "monochrome" },
  "mode": "wifi",
  "ip": "192.168.1.195",
  "capabilities": ["display", "clear"],
  "endpoints": ["POST /display", "POST /clear"]
}
```

### `POST /display` — Drawing Commands

```json
{
  "draw": [
    { "type": "text", "x": 0, "y": 13, "text": "Current Session", "size": 10 },
    { "type": "text", "x": 128, "y": 13, "text": "73%", "size": 10, "align": "right" },
    { "type": "bar", "x": 0, "y": 16, "w": 128, "h": 12, "pct": 73 }
  ]
}
```

支援：`text`（含 `align: "right"`）、`rect`、`bar`、`circle`、`line`、`pixel`

### 認證

所有 POST 需帶 `Authorization: Bearer <token>`。
Token 透過 USB Serial 的 `POST /provision` 產生（物理接觸 = 信任）。
GET 不需認證。

## Provisioning

```
開機
  ├─ 有儲存的 WiFi？→ 自動重連 → HTTP server 啟動
  └─ 沒有 → Serial 模式 → 等待 /monet-setup
```

Serial 和 HTTP 用相同的指令格式（`GET /info`、`POST /display` 等）。
WiFi credentials 和 token 存在 NVS，重開機自動恢復。

## 硬體 (MVP)

| 組件 | 型號 |
|------|------|
| MCU | ESP32-C3 Super Mini (RISC-V 160MHz, 400KB SRAM, Native USB-CDC) |
| 螢幕 | SSD1306 128x64 Monochrome OLED (I2C) |
| 接線 | GPIO5=SCL, GPIO6=SDA, 3.3V, GND |
| 字型 | ProFont (等寬) |

## MCP Server（規劃中）

包裝設備 HTTP API 為 MCP server，讓任何 MCP client 都能操作：

```
monet-mcp-server
  ├─ Tool: monet_display    → POST /display
  ├─ Tool: monet_clear      → POST /clear
  ├─ Resource: monet://info   → GET /info
  └─ Resource: monet://status → GET /status
```

## 產品線擴展

同一套 `/info` 自描述協議適用於不同硬體：

| 產品 | `/info` 描述 |
|------|-------------|
| Monochrome OLED | drawing commands、monochrome、text align |
| Color OLED/LCD | drawing commands、RGB color、framebuffer |
| E-ink | 刷新限制、黑白紅映射、`/update` + `/refresh` |
| LED Strip | 燈號控制、動畫序列 |
| Speaker | 音效播放、TTS |

Agent 不需要預先知道任何產品的細節。

## Repo 結構

```
water-lilies/
├── README.md
├── LICENSE                              MIT
├── DEVELOPMENT.md                       開發指南、已知問題
├── firmware/
│   └── water-lilies-mvp/
│       └── water-lilies-mvp.ino         ESP32-C3 + SSD1306 韌體
└── docs/
    ├── agent-native-iot-display.md      核心架構：API 規格、protocol
    ├── hardware-options.md              硬體方案比較與實測結論
    └── design-evolution.md              設計演進與決策紀錄

# Claude Code plugin 在獨立 repo：
# https://github.com/Monet-Ltd/monet-setup
```

## 命名

| 名稱 | 是什麼 |
|------|--------|
| **Monet Ltd.** | 公司 |
| **Water Lilies** | 產品名稱 / repo |
| `/monet-setup` | Claude Code slash command |
| `~/.monet/` | 本機設備設定檔 |
| `monet-mcp-server` | MCP server（規劃中）|

## Open Questions

- [ ] mDNS discovery（`water-lilies.local` 免記 IP）
- [ ] OTA 韌體更新機制
- [ ] 省電模式（長時間不更新時降亮度或關閉）
- [ ] MCP server 實作
- [ ] IP 變動偵測（DHCP 重分配後自動更新 config）
- [ ] Color display 產品（ESP32-S3 + ST7789）

## 設計決策紀錄

完整的設計演進和技術討論見 [docs/design-evolution.md](docs/design-evolution.md)。

## License

MIT License. See [LICENSE](LICENSE).
