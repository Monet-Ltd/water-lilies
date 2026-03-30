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
3. WiFi credentials + auth token 存 NVS，重開機自動恢復

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

## v8 — MVP 實作與硬體驗證

選定 ESP32-C3 Super Mini + SSD1306 128x64 I2C OLED 作為 MVP 硬體。

實測結果：
- 編譯 83% Flash / 12% RAM，WiFi 連線後 ~150KB free heap
- SSD1306 I2C framebuffer 僅 1KB（128x64 / 8），RAM 完全不是問題
- ProFont 等寬字型作為統一視覺風格
- text `align: "right"` 支援，用 `u8g2.getStrWidth()` 計算偏移
- Serial `readStringUntil('\n')` 預設 buffer 太小（~256B），改為手動 byte-by-byte 讀取

## v9 — Serial 不可行，WiFi only

**關鍵發現：ESP32-C3 USB-CDC 每次打開 serial port 都會 reset 晶片。**

```
rst:0x15 (USB_UART_CHIP_RESET)
```

這是硬體行為，軟體無法阻止：
- `stty -hupcl` → 只防止關閉時 reset，打開時仍然 reset
- `dsrdtr=False, rtscts=False` → USB-CDC 忽略這些信號
- 背景 daemon 保持 port 開啟 → 可行但過於複雜

**決策：Serial 僅用於一次性 provisioning（reset 可接受），持續推送全走 WiFi HTTP。**

這個限制是 USB-CDC 特有的。使用 CH340/CP2102 外接 USB-UART 的板子不受影響。

## v10 — statusLine 整合

將 WiFi HTTP 推送注入到現有的 Claude Code statusLine script：

- **追加不覆蓋** — 不動原本的 statusLine 功能
- **背景 curl** — `&>/dev/null &` 非阻塞，1 秒 timeout
- **純 shell** — 不依賴 python，用 string interpolation 組 JSON（避免 jq 產生過長 payload）
- 設備設定讀取 `~/.monet/devices.json`

## v11 — Plugin 結構

加入 `plugin.json` 讓 Claude Code 能載入 `/monet-setup` slash command：

```json
{
  "name": "water-lilies",
  "skills": [{ "name": "monet-setup", "path": "skills/monet-setup.md" }]
}
```

安裝：`claude plugin add ~/water-lilies`

## Review 紀錄

### Round 1 — 初始設計審查

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

| 問題 | 決策 |
|------|------|
| `/info` 自然語言 vs 結構化不清 | `/info` 純自然語言，`/status` 負責結構化 capability |
| pyserial 硬依賴 | Skill 使用 pyserial 做 provisioning，statusLine 推送用 curl |
| statusLine jq script 太重 | 改用 string interpolation 組 JSON |
| 產品命名不一致 | Monet Ltd.（公司）/ Water Lilies（產品）/ monet-setup（command） |
| 無 API 版本 | `/status` 加 `api_version` 欄位 |

### Round 3 — 實作驗證

| 問題 | 決策 |
|------|------|
| ESP32-C3 USB-CDC serial reset | Serial 僅限 provisioning，推送走 WiFi |
| Serial buffer 截斷長 JSON | 韌體改手動 byte 讀取 + pyserial 分 64B chunk 發送 |
| statusLine 背景推送被 kill | 改前景 curl 但發現不是這個問題，實際是環境變數傳遞 |
| 5GHz WiFi DHCP timeout | `/info` 和 skill 明確警告僅支援 2.4GHz |
| `collectHeaders` API 不相容 | ESP32 Arduino Core 3.x 需要陣列形式 |
