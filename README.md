# Water Lilies

> **Monet Ltd.** — Agent-native IoT display that describes itself.

Water Lilies 是一系列 IoT 顯示設備。每台設備在 `/info` endpoint 用自然語言描述自己的能力，
AI agent 讀一次就知道怎麼用。同一句 prompt 適配所有硬體型號，不需要針對每個產品寫驅動或文件。

```
用戶: /monet-setup 顯示 Claude Code 的 rate limit

Agent:
  1. GET http://192.168.1.50/info → 讀到自然語言使用手冊
  2. 理解螢幕大小、繪圖指令、認證方式
  3. 自動產生推送 script
  4. Done
```

## 架構

```
┌─────────────┐      USB Serial        ┌──────────────────┐
│  電腦 / Agent │◄─── (首次 setup) ────►│  Water Lilies     │
│             │                        │  IoT Device       │
│  curl POST  │───── WiFi HTTP ───────►│                   │
│  (推送資料)  │                        │  GET /info   (自述)│
│             │◄──── WiFi HTTP ────────│  GET /status (狀態)│
│             │                        │  POST /display    │
│             │                        │  POST /framebuffer│
└─────────────┘                        └──────────────────┘
```

**推送模式**：設備開 HTTP server，電腦有新資料就 POST 過去。不是輪詢。

## 設備 API

### `GET /info` — 自然語言使用手冊

設備用 plain text 描述自己的所有能力。消費者是 LLM，不是 parser。
內容根據設備狀態動態變化（Serial 模式 vs WiFi 模式顯示不同指引）。

### `GET /status` — 結構化狀態

```json
{
  "device": "Water Lilies Display v1.2",
  "api_version": "1.0",
  "screen": { "w": 240, "h": 240, "type": "color" },
  "mode": "wifi",
  "ip": "192.168.1.50",
  "capabilities": ["display", "framebuffer", "brightness"],
  "endpoints": ["POST /display", "POST /framebuffer", "POST /clear", "POST /brightness"]
}
```

### `POST /display` — Drawing Commands

JSON 描述繪圖指令。支援 `text`, `rect`, `bar`, `circle`, `line`, `pixel`, `image`。

```json
{
  "bg": "#000000",
  "draw": [
    { "type": "text", "x": 0, "y": 14, "text": "5h 73%", "color": "#00FF00", "size": 16 },
    { "type": "bar", "x": 0, "y": 20, "w": 128, "h": 8, "pct": 75, "fg": "#00FF00", "bg": "#333" }
  ]
}
```

### `POST /framebuffer` — Raw Pixels

RGB565 big-endian，完全自由度。適合圖片或自定渲染。

### 認證

所有 POST 需帶 `Authorization: Bearer <token>`。
Token 透過 USB Serial 的 `POST /provision` 產生（物理接觸 = 信任）。
GET 不需認證。

## Provisioning

```
開機
  ├─ 有儲存的 WiFi？→ 自動重連 → HTTP + Serial 雙模並存
  └─ 沒有 → 僅 Serial 模式 → 等待設定
```

Serial 和 HTTP 用**同樣的指令格式**：

```
GET /info
POST /wifi {"ssid":"MyNet","pass":"xxx"}
POST /provision
POST /display {"bg":"#000","draw":[...]}
```

Serial framing：request 以 `\n` 結尾，response 以 `\n---END---\n` 結尾。
WiFi 連上後 serial 仍可用，斷線不會變磚。

## 用戶端：`/monet-setup`

Claude Code slash command，一句話完成設定：

```bash
/monet-setup                              # 首次設定（USB → WiFi → token）
/monet-setup 顯示 Claude Code rate limit   # 設定 + 配置顯示
/monet-setup 改成顯示天氣和時間             # 已設定過，直接改
```

Agent 讀 `/info` 後根據 instruction 自動決定資料來源、佈局、更新頻率。

設備設定存在 `~/.monet/devices.json`。

## MCP Server（規劃中）

包裝設備 HTTP API 為 MCP server，讓任何 MCP client 都能操作：

```
monet-mcp-server
  ├─ Tool: monet_display      → POST /display
  ├─ Tool: monet_clear        → POST /clear
  ├─ Tool: monet_brightness   → POST /brightness
  ├─ Tool: monet_framebuffer  → POST /framebuffer
  ├─ Resource: monet://info   → GET /info
  └─ Resource: monet://status → GET /status
```

## 產品線擴展

同一套 `/info` 自描述協議適用於不同硬體：

| 產品 | `/info` 描述 |
|------|-------------|
| Color OLED/LCD | drawing commands、RGB color、framebuffer |
| E-ink | 刷新限制、黑白紅映射、`/update` + `/refresh` |
| LED Strip | 燈號控制、動畫序列 |
| Speaker | 音效播放、TTS |

Agent 不需要預先知道任何產品的細節。

## Repo 結構

```
water-lilies/
├── README.md                           ← 你在這裡
└── docs/
    ├── agent-native-iot-display.md     ← 核心架構：API 規格、protocol、provisioning 細節
    ├── hardware-options.md             ← 硬體方案比較（MCU、螢幕、一體板推薦）
    └── monet-setup-command.md          ← /monet-setup slash command 完整規格
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

- [ ] 硬體選型（見 `docs/hardware-options.md`）
- [ ] OTA 韌體更新機制
- [ ] mDNS discovery（`water-lilies.local` 免記 IP）
- [ ] 省電模式（長時間不更新時降亮度或關閉）
- [ ] MCP server 實作細節
- [ ] `monet-cli` standalone binary（免 pyserial 依賴）
- [ ] API 版本策略（major bump 時的相容處理）

## 設計決策紀錄

完整的設計演進和技術討論見 [docs/design-evolution.md](docs/design-evolution.md)。
