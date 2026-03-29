# /monet-setup — Claude Code Slash Command

> 一個 slash command 搞定 IoT display 的所有設定。

## 使用方式

```bash
# 基本設定（連線、WiFi、provision）
/monet-setup

# 設定 + 指定顯示內容
/monet-setup 顯示 Claude Code 的 rate limit，5h 和 7d 用不同顏色的 bar

# 更換顯示內容（已設定過的設備）
/monet-setup 改成顯示目前時間和天氣
```

## Command 行為流程

```
/monet-setup [instruction]
  │
  ├─ 有已儲存的設備設定？(~/.monet/devices.json)
  │   ├─ 有 → GET /status 確認設備在線
  │   │       ├─ 在線 + 有 instruction → 直接跳到「配置顯示」
  │   │       ├─ 在線 + 無 instruction → 「設備正常，要改顯示內容嗎？」
  │   │       └─ 離線 → 「設備無回應，要重新設定嗎？」
  │   └─ 沒有 → 進入首次設定流程
  │
  ├─ 首次設定流程
  │   1. 偵測 serial port（列出 /dev/tty.usb* 讓用戶選）
  │   2. 開 serial → GET /info → 讀到設備資訊
  │   3. 問用戶 WiFi SSID 和密碼
  │   4. POST /wifi → 拿到 IP
  │   5. POST /provision → 拿到 token
  │   6. GET http://<ip>/info → 確認 HTTP 正常
  │   7. 儲存到 ~/.monet/devices.json
  │   8. 如果有 instruction → 進入「配置顯示」
  │      如果沒有 → 「設定完成！下次用 /monet-setup <想顯示什麼> 來配置」
  │
  └─ 配置顯示
      1. 讀設備的 /info（知道螢幕大小、支援的 drawing types）
      2. 根據 instruction 決定：
         a. 資料來源（statusLine? cron? 外部 API?）
         b. 佈局（文字位置、bar 位置、顏色）
         c. 更新頻率
      3. 產生推送 script 或修改 statusLine config
      4. 測試推送一次，確認螢幕有畫面
      5. 「配置完成！」
```

## 設備設定檔

`~/.monet/devices.json`：

```json
{
  "devices": [
    {
      "name": "desk-display",
      "ip": "192.168.1.50",
      "token": "a1b2c3d4e5f6",
      "screen": { "w": 240, "h": 240, "type": "color" },
      "device_info": "Pixie Display v1.2 / ST7789",
      "serial_port": "/dev/tty.usbserial-14210",
      "added_at": "2026-03-29"
    }
  ],
  "active": "desk-display"
}
```

## Skill 定義

```markdown
---
name: monet-setup
description: >
  Set up and configure a Monet IoT display device. Handles first-time provisioning
  (serial → WiFi → token) and display configuration via natural language instructions.
  Run without arguments for setup, or with instructions to configure what to display.
user_invocable: true
arguments:
  - name: instruction
    required: false
    description: What to display and how (natural language)
---

You are configuring a Monet IoT display — a hardware device with a color screen
that accepts drawing commands over HTTP.

## Step 1: Check existing config

Read `~/.monet/devices.json`. If it exists and has devices:
- GET `http://<ip>/status` to check if the device is online.
- If online and user provided an instruction, skip to Step 5.
- If online and no instruction, tell the user the device is working and ask what they want to display.
- If offline, ask if they want to re-setup or try a different IP.

If no config exists, proceed to Step 2.

## Step 2: Detect serial port

List available serial ports:
\`\`\`bash
ls /dev/tty.usb* 2>/dev/null || ls /dev/ttyUSB* 2>/dev/null || ls /dev/ttyACM* 2>/dev/null
\`\`\`

If multiple ports found, ask the user to pick one.
If none found, tell the user to connect the device via USB.

## Step 3: Serial provisioning

Use python3 with pyserial to communicate (most reliable cross-platform):

\`\`\`bash
python3 -c "
import serial, sys
ser = serial.Serial('PORT', 115200, timeout=5)
ser.write(b'GET /info\n')
resp = ''
while True:
    line = ser.readline().decode('utf-8', errors='replace')
    if '---END---' in line: break
    resp += line
print(resp)
ser.close()
"
\`\`\`

Read the /info response to understand the device.

Ask the user for WiFi SSID and password, then:

\`\`\`bash
python3 -c "
import serial, json
ser = serial.Serial('PORT', 115200, timeout=15)
ser.write(b'POST /wifi {\"ssid\":\"SSID\",\"pass\":\"PASS\"}\n')
resp = ''
while True:
    line = ser.readline().decode('utf-8', errors='replace')
    if '---END---' in line: break
    resp += line
print(resp)
ser.close()
"
\`\`\`

If OK, continue. If ERR:*, explain the error and retry.

Then provision a token:

\`\`\`bash
python3 -c "
import serial
ser = serial.Serial('PORT', 115200, timeout=5)
ser.write(b'POST /provision\n')
resp = ''
while True:
    line = ser.readline().decode('utf-8', errors='replace')
    if '---END---' in line: break
    resp += line
print(resp)
ser.close()
"
\`\`\`

## Step 4: Verify HTTP and save config

GET `http://<ip>/info` to confirm HTTP is working.
GET `http://<ip>/status` to get device details.

Save to `~/.monet/devices.json`:
- ip, token, screen dimensions, device name, serial port, timestamp

If no instruction was provided, stop here and report success.

## Step 5: Configure display

Read the device's `/info` to know:
- Screen size (w, h)
- Supported drawing types
- Color support

Based on the user's natural language instruction, determine:

1. **Data source**: Where does the data come from?
   - Claude Code statusLine → modify statusLine config to POST to device
   - System info (time, CPU, etc.) → create a cron script
   - External API (weather, stocks, etc.) → create a cron script
   - Static content → one-time POST

2. **Layout**: Design the drawing commands JSON
   - Respect screen dimensions from /info
   - Use appropriate text sizes for the screen
   - Choose colors that match the instruction's mood

3. **Update mechanism**: How often to push
   - statusLine: every time Claude Code gets a response (automatic)
   - Cron: create a launchd plist or crontab entry
   - One-time: just POST once

4. **Test**: Push once and ask the user to confirm the screen looks right.

## Example: statusLine integration

If the instruction involves Claude Code data (rate limit, model, tokens):

Add to Claude Code settings (`~/.claude/settings.json`) statusLine:

\`\`\`bash
input=$(cat)
# Extract data
pct5h=$(echo "$input" | jq -r '.rate_limits.five_hour.used_percentage // empty')
pct7d=$(echo "$input" | jq -r '.rate_limits.seven_day.used_percentage // empty')
model=$(echo "$input" | jq -r '.model // empty')

# Push to Monet display (non-blocking)
[ -n "$pct5h" ] && curl -s -m 1 -X POST "http://DEVICE_IP/display" \
  -H "Authorization: Bearer DEVICE_TOKEN" \
  -H "Content-Type: application/json" \
  -d "$(jq -n --arg p5 "$pct5h" --arg p7 "${pct7d:-0}" --arg m "${model:-unknown}" '{
    bg: "#000000",
    draw: [
      {type:"text", x:4, y:16, text:("5h " + $p5 + "%"), color:(if ($p5|tonumber) > 80 then "#FF4444" elif ($p5|tonumber) > 50 then "#FFAA00" else "#00FF88" end), size:16},
      {type:"bar", x:4, y:22, w:232, h:8, pct:($p5|tonumber), fg:(if ($p5|tonumber) > 80 then "#FF4444" elif ($p5|tonumber) > 50 then "#FFAA00" else "#00FF88" end), bg:"#222222"},
      {type:"text", x:4, y:52, text:("7d " + $p7 + "%"), color:"#8888FF", size:16},
      {type:"bar", x:4, y:58, w:232, h:8, pct:($p7|tonumber), fg:"#8888FF", bg:"#222222"},
      {type:"text", x:4, y:90, text:$m, color:"#666666", size:12}
    ]
  }')" &>/dev/null &

# Normal statusLine output
[ -n "$pct5h" ] && printf "5h: %.0f%%" "$pct5h"
\`\`\`

## Important notes

- Always check pyserial is installed: `pip3 install pyserial` if needed
- WiFi password is sensitive — never log it or save it in plain text outside the device
- Token should be stored in ~/.monet/devices.json with restricted permissions (chmod 600)
- If the user has multiple devices, ask which one to configure
- Always test with a single POST before committing to a recurring setup
```

## 多設備支援

```bash
# 設定第二個設備
/monet-setup

# 指定設備名稱
/monet-setup --device bedroom-display 顯示天氣和時間

# 列出所有設備
/monet-setup --list
```

## 各種 instruction 範例

| Instruction | Agent 行為 |
|-------------|-----------|
| `顯示 Claude Code rate limit` | 修改 statusLine，推送 5h/7d bar |
| `顯示目前時間` | 建立 cron script，每分鐘推送 |
| `顯示天氣` | 建立 cron script，呼叫天氣 API，每 30 分鐘推送 |
| `顯示 CPU 和記憶體使用量` | 建立 cron script，每 5 秒推送系統資訊 |
| `顯示一隻貓` | 用 framebuffer endpoint 推送圖片 |
| `用紅色大字顯示 DEPLOY FAILED` | 一次性推送靜態畫面 |
| `把螢幕當成 git status 看板` | 建立 git hook，push/pull 時推送狀態 |

Agent 不需要預先知道這些場景 — 它讀 `/info` 知道設備能力，根據 instruction 自由發揮。
