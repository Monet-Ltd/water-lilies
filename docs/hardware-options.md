# Hardware Options — Agent-Native IoT Display

> 尚未選定硬體方案。以下列出各種組合的優缺點，供決策參考。

## 需求摘要

必須有：OLED/LCD 彩色螢幕、WiFi、USB Serial (UART)
加分項：BLE、大螢幕、低功耗、體積小

---

## 方案一覽

### MCU 選擇

| MCU | WiFi | BLE | Flash | RAM | USB | 價格 (USD) | 備註 |
|-----|------|-----|-------|-----|-----|-----------|------|
| **ESP32-S3** | 2.4GHz | 5.0 | 8-16MB | 512KB+PSRAM | Native USB | ~$3 | **推薦**，USB-CDC 免 CH340，PSRAM 適合 framebuffer |
| ESP32-C3 | 2.4GHz | 5.0 | 4MB | 400KB | USB-Serial | ~$2 | 便宜但 RAM 吃緊，沒 PSRAM |
| ESP32 (classic) | 2.4GHz | 4.2 | 4-16MB | 520KB | 需 CH340/CP2102 | ~$2.5 | 老牌穩定，但需外接 USB 轉 serial 晶片 |
| RP2040-W (Pico W) | 2.4GHz | - | 2MB | 264KB | Native USB | ~$6 | 無 BLE，RAM 小，但雙核穩定 |
| RP2350-W (Pico 2W) | 2.4GHz | BLE | 4MB | 520KB | Native USB | ~$7 | 新款，規格升級，生態還在成長 |
| nRF52840 + WiFi 模組 | 需外掛 | 5.0 | 1MB | 256KB | Native USB | ~$10+ | BLE 王者但 WiFi 要額外模組，太複雜 |

### 螢幕選擇

| 螢幕 | 尺寸 | 解析度 | 色彩 | 介面 | 價格 (USD) | 備註 |
|------|------|--------|------|------|-----------|------|
| **SSD1351 OLED** | 1.5" | 128x128 | 65K RGB | SPI | ~$8 | 色彩飽和、對比無限，尺寸適中 |
| **ST7789 IPS LCD** | 1.3"-2.4" | 240x240 | 65K RGB | SPI | ~$4 | 便宜、亮度高、選擇多，**性價比最佳** |
| SSD1306 OLED | 0.96" | 128x64 | 單色 | I2C/SPI | ~$2 | 太小、單色，不符合「彩色可自訂」需求 |
| ST7735 LCD | 1.8" | 128x160 | 65K RGB | SPI | ~$3 | 便宜但色彩偏差，viewing angle 差 |
| GC9A01 圓形 LCD | 1.28" | 240x240 | 65K RGB | SPI | ~$5 | 圓形很酷，但 UI 佈局麻煩 |
| ILI9341 LCD | 2.4"-3.2" | 320x240 | 65K RGB | SPI/8bit | ~$6 | 大螢幕，資訊量多，framebuffer 大 (150KB) |
| Waveshare e-Paper | 2.9" | 296x128 | 黑白紅 | SPI | ~$15 | 超省電、戶外可讀，但刷新慢 (3-15s) |

---

## 推薦組合

### Tier 1: 推薦方案

#### A. ESP32-S3 + ST7789 1.54" 240x240 — 「性價比之王」

```
總成本: ~$7-10
螢幕: 240x240 IPS, 色彩好, 可視角大
MCU: WiFi + BLE, Native USB, PSRAM
Framebuffer: 240*240*2 = 112KB (需 PSRAM)
```

- 最多現成開發板可選（ESP32-S3-DevKitC + 外接螢幕，或一體板）
- ST7789 library 成熟（TFT_eSPI, LovyanGFX）
- 240x240 比 128x128 多 3.5 倍像素，資訊密度高
- Native USB 免焊接，直接 USB-C 連電腦

#### B. ESP32-S3 + SSD1351 1.5" 128x128 — 「OLED 畫質黨」

```
總成本: ~$11-14
螢幕: 128x128 OLED, 真黑、無限對比、色彩飽和
MCU: 同上
Framebuffer: 128*128*2 = 32KB (SRAM 就夠)
```

- OLED 暗場景下顯示效果遠勝 LCD
- Framebuffer 小，不需要 PSRAM
- 缺點：螢幕貴一點，亮度不如 LCD，有燒屏風險

### Tier 2: 特殊用途

#### C. ESP32-S3 + ILI9341 2.4" 320x240 — 「資訊看板」

```
總成本: ~$9-12
螢幕: 大、清楚，可以同時顯示很多資訊
Framebuffer: 320*240*2 = 150KB (必須 PSRAM)
```

- 適合固定放在桌上當 dashboard
- 體積大，不適合嵌入或攜帶

#### D. ESP32-C3 + ST7789 1.3" 240x240 — 「極致便宜」

```
總成本: ~$5-7
螢幕: 小但夠用
MCU: WiFi + BLE, 但 RAM 只有 400KB, 無 PSRAM
```

- 不能用 framebuffer endpoint（RAM 不夠放 112KB buffer）
- 只能用 drawing commands API
- 適合只顯示文字和簡單圖形的場景

#### E. ESP32-S3 + GC9A01 圓形 1.28" 240x240 — 「桌面小物」

```
總成本: ~$8-11
螢幕: 圓形，像小手錶，桌面擺飾感
```

- 視覺很有辨識度
- UI 要處理圓形裁切，drawing commands 需要 clip region
- `/info` 需額外描述 usable area

#### F. 任意板 + Waveshare e-Paper 2.9" — 「省電掛牆」

```
總成本: ~$18-22
螢幕: 不耗電、戶外可讀、不傷眼
```

- 刷新慢 (3-15 秒)，不適合即時更新
- 適合低頻資料（每小時更新一次的 dashboard）
- API 設計不同：沒有 `/framebuffer` 即時推送，改用 `/update` + 確認機制
- 黑白紅三色，drawing commands 的 color 要映射到三值

---

## 現成一體板推薦

不想自己接線的話：

| 開發板 | MCU | 螢幕 | USB | 價格 | 來源 |
|--------|-----|------|-----|------|------|
| **LILYGO T-Display-S3** | ESP32-S3 | 1.9" ST7789 170x320 | USB-C | ~$15 | AliExpress/Amazon |
| **LILYGO T-Display-S3 AMOLED** | ESP32-S3 | 1.91" AMOLED 240x536 | USB-C | ~$22 | 色彩最好 |
| Waveshare ESP32-S3-Touch-LCD-1.28 | ESP32-S3 | 1.28" GC9A01 圓形 | USB-C | ~$12 | 圓形觸控 |
| M5Stack Core2 | ESP32 | 2.0" ILI9342 320x240 | USB-C | ~$45 | 完整外殼、電池、喇叭 |
| M5StickC Plus2 | ESP32-S3 | 1.14" ST7789 135x240 | USB-C | ~$20 | 口袋大小、有電池 |
| Seeed XIAO ESP32S3 + 擴展板 | ESP32-S3 | 外接 | USB-C | ~$8+螢幕 | 最小體積 |

**如果只買一個試水溫：LILYGO T-Display-S3**。一體板、USB-C、螢幕夠大、~$15、社群多。

---

## 硬體選擇對 API 設計的影響

| 硬體差異 | 對 `/info` 和 API 的影響 |
|----------|-------------------------|
| 螢幕解析度不同 | `/info` 報告 `screen.w` / `screen.h`，agent 自動適配座標 |
| 有無 PSRAM | 沒有 → `/info` 不列 `/framebuffer` endpoint |
| 單色 vs 彩色 | 單色 → color 值自動映射為亮度閾值，`/info` 說明 |
| e-Paper 慢刷新 | `/info` 描述 refresh 限制，API 改為 `/update` + `/refresh` |
| 圓形螢幕 | `/info` 包含 `shape: "circle"` 和 usable rect area |
| 有觸控 | `/info` 列出 touch event endpoint（`GET /events`） |
| 有喇叭 | `/info` 列出 `POST /sound` endpoint |
| 有電池 | `/status` 包含 `battery_pct` |

這就是 agent-native 的威力：**不管硬體怎麼變，agent 讀 `/info` 就自動知道該怎麼用**。

---

## 決策待定

- [ ] 目標場景（桌面擺飾？掛牆 dashboard？隨身攜帶？）
- [ ] 預算範圍
- [ ] 是否需要觸控互動
- [ ] 是否需要電池供電
- [ ] 是否需要外殼（3D 列印？現成？）
- [ ] 首批數量（自用 prototype？還是要做成產品？）
