# Water Lilies

> **Monet Ltd.** — Agent-native IoT display that describes itself.

AI agent 讀設備的 `/info`，用自然語言理解它的能力，自動完成設定和推送。
同一句 prompt 適配所有硬體型號。

## 核心想法

傳統 IoT：讀 datasheet → 找 SDK → 寫驅動 → 對接 API。

Water Lilies：設備自己用自然語言告訴 agent 怎麼用。

```
用戶: /monet-setup 顯示 Claude Code 的 rate limit

Agent:
  1. 讀 ~/.monet/devices.json 找到設備 IP
  2. GET http://192.168.1.50/info → 讀到自然語言使用手冊
  3. 理解螢幕大小、支援的繪圖指令、認證方式
  4. 自動產生推送 script，配置 statusLine
  5. Done
```

## 設計演進

### v0 — 起點：能不能把 rate limit 顯示在實體螢幕上？

Claude Code 的 statusLine 機制會在每次 API response 後執行一段 shell command，
stdin 帶有 `rate_limits` JSON。這些資料來自 claude.ai 的回應，不需要額外 API 呼叫。

最初想法：讓電腦開 HTTP server，IoT 設備來輪詢。

### v1 — 反轉：IoT 開 server，電腦推送

輪詢浪費電，而且資料更新頻率不固定。反過來讓 IoT 設備開 HTTP server，
電腦有新資料就 `curl POST` 過去。

- IoT 邏輯簡單 — 收到什麼畫什麼
- 電腦端不用跑額外 server
- 即時更新，不浪費頻寬

### v2 — 自訂顯示：Drawing Commands + Framebuffer

用戶想自訂畫面，不只是固定的 rate limit bar。兩種通道：

- **Drawing Commands** (`POST /display`) — JSON 描述文字、矩形、圓、線、進度條、圖片，~200B
- **Framebuffer** (`POST /framebuffer`) — Raw RGB565 pixels，完全自由度，~32KB

### v3 — Agent-native：`/info` 自我描述

關鍵突破：設備在 `/info` endpoint 用**自然語言**描述自己的全部能力。
Agent 讀一次就知道怎麼用，不需要針對每個型號寫不同的驅動或文件。

這就是 MCP 的精神套用在實體硬體上 — capability discovery，但消費者是 LLM。

不同產品回傳不同的 `/info`：
- Color OLED → 描述 drawing commands、color format
- E-ink → 描述刷新限制、黑白紅三色映射
- LED Strip → 描述燈號控制 API
- Speaker → 描述音效播放 API

**Agent 不需要預先知道任何產品的細節。**

### v4 — Provisioning：有線開箱設定

新設備沒有 WiFi 資訊，無法無線連接。解法：

1. 開機預設 USB Serial 模式
2. Serial 和 HTTP 用**同樣的指令格式**（`GET /info`, `POST /wifi`），agent 不用學兩套
3. 連上 WiFi 後 serial 仍然可用，WiFi 斷線不會變磚（雙模並存）
4. WiFi credentials + auth token 存 NVS，重開機自動恢復

Serial protocol 定義了 framing：request 以 `\n` 結尾，response 以 `\n---END---\n` 結尾。
錯誤碼結構化：`ERR:SSID_NOT_FOUND`、`ERR:AUTH_FAILED` 等。

### v5 — 安全：Provisioning Token

任何人在同網段都能 POST — 不行。

- `POST /provision`（僅 serial）產生隨機 token
- 之後所有 HTTP POST 需帶 `Authorization: Bearer <token>`
- `GET /info` 和 `GET /status` 不需認證（唯讀）
- Token 存 NVS，重開機不遺失
- 物理接觸 = 信任，token 只能透過 serial 重新產生

### v6 — `/status` 輕量查詢

`/info` 是完整自然語言文件，首次 setup 用。但日常使用每次都讀太浪費 token。

`GET /status` 回傳精簡 JSON：

```json
{
  "device": "Water Lilies Display v1.2",
  "api_version": "1.0",
  "screen": { "w": 240, "h": 240, "type": "color" },
  "mode": "wifi",
  "ip": "192.168.1.50",
  "wifi_rssi": -42,
  "uptime_s": 86400,
  "capabilities": ["display", "framebuffer", "brightness"],
  "endpoints": ["POST /display", "POST /framebuffer", "POST /clear", "POST /brightness"]
}
```

職責分離：
- `/info` — 自然語言，給 agent 理解「怎麼用」
- `/status` — 結構化 JSON，給 agent 程式化讀取「能做什麼」

### v7 — `/monet-setup` Slash Command

用戶端體驗壓縮到一個 slash command：

```bash
/monet-setup                              # 首次設定
/monet-setup 顯示 Claude Code rate limit   # 設定 + 配置顯示
/monet-setup 改成顯示天氣                   # 已設定過，直接改
```

Agent 根據 instruction 自動決定資料來源（statusLine / cron / git hook / 一次性）、
佈局、更新頻率，推送一次測試，完成。

### v8 — 跨平台 Serial 通訊

Serial 通訊不能只靠 `python3 + pyserial`。不同平台有不同的方式：

skill 應包含多種 fallback 策略：

| 平台 | 優先方式 | 備選 |
|------|---------|------|
| macOS | `python3 -c "import serial..."` | `screen`, `stty + echo > /dev/tty.*` |
| Linux | 同上 | `minicom`, `stty` |
| Windows | `python -c "import serial..."` | PowerShell `[System.IO.Ports.SerialPort]` |

Skill 先偵測平台和可用工具，選最佳路徑。如果 pyserial 不在，提示安裝或 fallback。

長期可以考慮附帶一個 `monet-cli` standalone binary（Go/Rust），免依賴。

### v9 — MCP Server 包裝

設備的 HTTP API 可以包裝成 MCP server，讓任何支援 MCP 的 AI agent 都能操作：

```
monet-mcp-server
  ├─ Tool: monet_display    → POST /display
  ├─ Tool: monet_clear      → POST /clear
  ├─ Tool: monet_brightness → POST /brightness
  ├─ Tool: monet_framebuffer → POST /framebuffer
  ├─ Resource: monet://info   → GET /info
  └─ Resource: monet://status → GET /status
```

好處：
- 不限 Claude Code — Cursor、VS Code Copilot、任何 MCP client 都能用
- Tool schema 提供類型安全，resource 提供 capability discovery
- 多設備用 `monet://desk-display/status` URI 區分

MCP server 只是 HTTP API 的薄 wrapper，核心邏輯還是在設備上。

### v10 — API 版本策略

韌體更新可能改 API。需要版本號讓 agent 和 MCP server 知道怎麼適配。

- `/status` 回傳 `api_version: "1.0"`
- `/info` 自然語言裡也提及版本
- MCP server 根據 `api_version` 調整 tool schema
- 重大破壞性變更 bump major version，agent skill 可以根據版本分支處理

## 文件結構

```
water-lilies/
├── README.md                              ← 你在這裡
└── docs/
    ├── agent-native-iot-display.md        ← 核心架構：API、protocol、provisioning
    ├── hardware-options.md                ← 硬體方案比較（MCU、螢幕、一體板）
    └── monet-setup-command.md             ← /monet-setup slash command 規格
```

## 命名

- **Monet Ltd.** — 公司
- **Water Lilies** — 產品名稱（this repo）
- **`/monet-setup`** — Claude Code slash command
- **`~/.monet/`** — 本機設備設定檔目錄
- **`monet-mcp-server`** — MCP server（未來）

## Open Questions

- [ ] 硬體選型（見 `docs/hardware-options.md`）
- [ ] OTA 韌體更新機制
- [ ] 多 source 同時推送的行為定義（目前假設一台電腦一個進程推一台設備）
- [ ] mDNS discovery（`water-lilies.local` 免記 IP）
- [ ] 省電模式（長時間不更新時降亮度或關閉）
- [ ] MCP server 實作細節
- [ ] `monet-cli` standalone binary 是否需要
- [ ] statusLine 推送 script 拆成獨立 `~/.monet/push.sh`，避免 statusLine 過重
- [ ] `~/.monet/devices.json` 的 token 是否需要用 Keychain 存儲（或至少警告不要 sync dotfiles）
