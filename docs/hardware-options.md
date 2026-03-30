# Hardware Options — Water Lilies

> 硬體方案比較與 MVP 實測結論。

## MVP 實測結論

### 選定：ESP32-C3 Super Mini + SSD1306 128x64 I2C OLED

實測驗證：

| 項目 | 結果 |
|------|------|
| WiFi + HTTP server + JSON + Display | 正常運行，~150KB free heap |
| Flash 使用 | 83% (1.1MB / 1.3MB) |
| RAM 使用 | 12% global，運行時 ~180KB free |
| SSD1306 framebuffer | 1KB (128*64/8)，完全不是問題 |
| USB-CDC serial provisioning | 可用，但每次開 port 會 reset 晶片 |
| NVS 持久化 | WiFi + token 重開機自動恢復 |
| 2.4GHz WiFi | 正常，5GHz 不支援 |

### 已知限制

- **USB-CDC reset**：ESP32-C3 原生 USB 每次打開 serial port 會 reset 晶片。Serial 不能用於持續推送，僅限一次性 provisioning。
- **僅 2.4GHz WiFi**：5GHz SSID 連線失敗（DHCP timeout）。
- **無 PSRAM**：不能用 240x240 full framebuffer (115KB)。SSD1306 128x64 單色 framebuffer 僅 1KB，沒問題。
- **GPIO 有限**：11 腳可用，SPI 顯示器需 5-6 腳，I2C 只需 2 腳。

## MCU 選擇

| MCU | WiFi | BLE | RAM | USB | 備註 |
|-----|------|-----|-----|-----|------|
| **ESP32-C3 Super Mini** | 2.4GHz | 5.0 | 400KB | Native USB-CDC | **MVP 選定**，便宜小巧，但 USB-CDC 會 reset |
| ESP32-S3 Super Mini | 2.4GHz | 5.0 | 512KB+PSRAM | USB-OTG | 推薦升級，同尺寸，多 ~$2，無 reset 問題（如有 CH340 版） |
| ESP32 (classic) | 2.4GHz | 4.2 | 520KB | CH340/CP2102 | 外接 USB-UART，serial 不會 reset，但板子更大 |

### ESP32-C3 vs ESP32-S3 Super Mini

| | C3 Super Mini | S3 Super Mini |
|---|---|---|
| CPU | 單核 RISC-V 160MHz | 雙核 Xtensa 240MHz |
| SRAM | 400KB | 512KB |
| PSRAM | 無 | 可選 2MB |
| USB | CDC (會 reset) | OTG (更靈活) |
| 240x240 framebuffer | 不行 | 可以 (PSRAM) |
| 尺寸 | 22.5x18mm | 22.5x18mm (同) |

**產品化建議**：用 ESP32-S3 Super Mini with PSRAM。多 $2 但消除 RAM 焦慮和 USB reset 問題。

## 螢幕選擇

| 螢幕 | 尺寸 | 解析度 | 色彩 | 介面 | 備註 |
|------|------|--------|------|------|------|
| **SSD1306 OLED** | 0.96" | 128x64 | 單色 | I2C | **MVP 選定**，最簡單，2 線接 |
| ST7789 IPS LCD | 1.3"-2.4" | 240x240 | 65K RGB | SPI | 性價比最佳，需 PSRAM |
| SSD1351 OLED | 1.5" | 128x128 | 65K RGB | SPI | 色彩飽和，32KB framebuffer |
| GC9A01 圓形 LCD | 1.28" | 240x240 | 65K RGB | SPI | 圓形造型，UI 佈局要處理裁切 |

## 現成一體板

| 開發板 | MCU | 螢幕 | USB |
|--------|-----|------|-----|
| **LILYGO T-Display-S3** | ESP32-S3 | 1.9" ST7789 170x320 | USB-C |
| LILYGO T-Display-S3 AMOLED | ESP32-S3 | 1.91" AMOLED 240x536 | USB-C |
| M5StickC Plus2 | ESP32-S3 | 1.14" ST7789 135x240 | USB-C |

## 硬體對 API 的影響

| 硬體差異 | `/info` 和 API 的變化 |
|----------|----------------------|
| 螢幕解析度不同 | `/status` 報告 `screen.w` / `screen.h`，agent 自動適配 |
| 單色 vs 彩色 | 單色不需 color 參數，彩色加 `"color": "#RRGGBB"` |
| 有無 PSRAM | 沒有 → 不提供 `/framebuffer` endpoint |
| 有觸控 | `/info` 列出 touch event endpoint |
| USB-CDC vs CH340 | CDC 板子 serial 會 reset，`/info` 描述此限制 |

**不管硬體怎麼變，agent 讀 `/info` 就自動知道該怎麼用。**
