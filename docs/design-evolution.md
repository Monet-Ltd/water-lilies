# Design Evolution

> Water Lilies 從一個簡單的想法到完整產品設計的迭代過程。

## v0 — 起點：能不能把 rate limit 顯示在實體螢幕上？

Claude Code 的 statusLine 機制會在每次 API response 後執行一段 shell command，
stdin 帶有 `rate_limits` JSON。這些資料來自 claude.ai 的回應，不需要額外 API 呼叫。

最初想法：讓電腦開 HTTP server，IoT 設備來輪詢。

## v1 — 反轉：IoT 開 server，電腦推送

輪詢浪費電，而且資料更新頻率不固定。反過來讓 IoT 設備開 HTTP server，
電腦有新資料就 `curl POST` 過去。

- IoT 邏輯簡單 — 收到什麼畫什麼
- 電腦端不用跑額外 server
- 即時更新，不浪費頻寬

## v2 — 自訂顯示：Drawing Commands + Framebuffer

用戶想自訂畫面，不只是固定的 rate limit bar。兩種通道：

- **Drawing Commands** (`POST /display`) — JSON 描述文字、矩形、圓、線、進度條、圖片，~200B
- **Framebuffer** (`POST /framebuffer`) — Raw RGB565 pixels，完全自由度，~32KB

## v3 — Agent-native：`/info` 自我描述

關鍵突破：設備在 `/info` endpoint 用**自然語言**描述自己的全部能力。
Agent 讀一次就知道怎麼用，不需要針對每個型號寫不同的驅動或文件。

這就是 MCP 的精神套用在實體硬體上 — capability discovery，但消費者是 LLM。

不同產品回傳不同的 `/info`：
- Color OLED → 描述 drawing commands、color format
- E-ink → 描述刷新限制、黑白紅三色映射
- LED Strip → 描述燈號控制 API
- Speaker → 描述音效播放 API

**Agent 不需要預先知道任何產品的細節。**

## v4 — Provisioning：有線開箱設定

新設備沒有 WiFi 資訊，無法無線連接。解法：

1. 開機預設 USB Serial 模式
2. Serial 和 HTTP 用**同樣的指令格式**（`GET /info`, `POST /wifi`），agent 不用學兩套
3. 連上 WiFi 後 serial 仍然可用，WiFi 斷線不會變磚（雙模並存）
4. WiFi credentials + auth token 存 NVS，重開機自動恢復

Serial protocol 定義了 framing：request 以 `\n` 結尾，response 以 `\n---END---\n` 結尾。
錯誤碼結構化：`ERR:SSID_NOT_FOUND`、`ERR:AUTH_FAILED` 等。

## v5 — 安全：Provisioning Token

任何人在同網段都能 POST — 不行。

- `POST /provision`（僅 serial）產生隨機 token
- 之後所有 HTTP POST 需帶 `Authorization: Bearer <token>`
- `GET /info` 和 `GET /status` 不需認證（唯讀）
- Token 存 NVS，重開機不遺失
- 物理接觸 = 信任，token 只能透過 serial 重新產生

## v6 — `/status` 輕量查詢

`/info` 是完整自然語言文件，首次 setup 用。但日常使用每次都讀太浪費 token。

`GET /status` 回傳精簡 JSON，包含結構化的 capability 資訊。

職責分離：
- `/info` — 自然語言，給 agent 理解「怎麼用」
- `/status` — 結構化 JSON，給 agent 程式化讀取「能做什麼」

## v7 — `/monet-setup` Slash Command

用戶端體驗壓縮到一個 slash command。Agent 根據 instruction 自動決定
資料來源（statusLine / cron / git hook / 一次性）、佈局、更新頻率。

## v8 — 跨平台 Serial 通訊

Serial 通訊不能只靠 `python3 + pyserial`。Skill 應包含多種 fallback 策略：

| 平台 | 優先方式 | 備選 |
|------|---------|------|
| macOS | `python3 + pyserial` | `screen`, `stty` |
| Linux | `python3 + pyserial` | `minicom`, `stty` |
| Windows | `python + pyserial` | PowerShell `SerialPort` |

長期考慮附帶 `monet-cli` standalone binary（Go/Rust），免依賴。

## v9 — MCP Server 包裝

設備的 HTTP API 包裝成 MCP server，讓任何 MCP client 都能操作。
MCP server 只是 HTTP API 的薄 wrapper，核心邏輯在設備上。

## v10 — API 版本策略

- `/status` 回傳 `api_version`
- MCP server 根據版本調整 tool schema
- 重大破壞性變更 bump major version

## Review 紀錄

### Round 1 — 初始設計審查

發現的問題與修正：

| 問題 | 修正 |
|------|------|
| 無認證機制 | 加入 provisioning token（serial-only 產生） |
| Serial/WiFi 互斥切換，WiFi 斷線變磚 | 改為雙模並存 |
| Serial 無 framing，response 沒有結束邊界 | 定義 `\n---END---\n` delimiter |
| WiFi 設定掉電消失 | NVS 持久化 |
| 每次都讀 `/info` 浪費 token | 新增 `/status` 輕量查詢 |
| WiFi 錯誤只回 `FAIL` | 結構化錯誤碼 `ERR:SSID_NOT_FOUND` 等 |
| 缺少 image drawing type | 加入 `{ "type":"image", "data":"<base64>" }` |
| Framebuffer endianness 不明確 | 加 byte-level 範例 |

### Round 2 — 跨文件審查

發現的問題與決策：

| 問題 | 決策 |
|------|------|
| `/info` 自然語言 vs 結構化不清 | `/info` 純自然語言，`/status` 負責結構化 capability |
| pyserial 硬依賴 | Skill 加跨平台 fallback，長期考慮 monet-cli |
| statusLine jq script 太重 | 拆成獨立 push script |
| 產品命名不一致 | Monet Ltd.（公司）/ Water Lilies（產品）/ monet-setup（command） |
| 無 API 版本 | `/status` 加 `api_version` 欄位 |
| 多 source 推送競爭 | 假設一台電腦一個進程推一台設備 |
| MCP 擴展性 | 設備 HTTP API 包裝為 MCP server |
