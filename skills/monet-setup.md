---
name: monet-setup
description: >
  Set up a Water Lilies IoT display device. Handles USB serial provisioning,
  WiFi configuration, token generation, and automatic statusLine integration.
  The full flow: detect device → connect WiFi → provision token → inject statusLine push → done.
user_invocable: true
arguments:
  - name: instruction
    required: false
    description: What to display (default is Claude Code rate limit bars)
---

You are setting up a Water Lilies IoT display — a small OLED screen that shows Claude Code rate limits in real time.

The goal is fully automated: after this command finishes, the OLED automatically updates every time Claude Code gets an API response.

**IMPORTANT: WiFi is required.** The ESP32-C3 Super Mini uses native USB-CDC, which resets the chip every time the serial port is opened. Serial cannot be used for ongoing data push — only for one-time provisioning.

## Step 1: Check existing config

```bash
cat ~/.monet/devices.json 2>/dev/null
```

If config exists:
- `curl -s -m 2 http://<ip>/status` to check device is online
- If online, skip to Step 5 (statusLine integration)
- If offline, ask: "設備無回應。可能是 IP 變了或 WiFi 斷了，要重新設定嗎？"

If no config, proceed to Step 2.

## Step 2: Detect serial port

```bash
ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

- Multiple ports → ask user to pick
- None found → "請用 USB 線連接 Water Lilies 設備到電腦"

## Step 3: Serial provisioning

Serial is used ONLY for first-time setup (WiFi + token). Every serial port open resets the ESP32-C3 — this is expected and fine for provisioning.

Check pyserial:
```bash
python3 -c "import serial" 2>/dev/null && echo "OK" || echo "MISSING"
```
If MISSING: `pip3 install pyserial`

### 3a. Read device info, connect WiFi, and provision token

Do all three in a single serial session to avoid multiple resets:

**BEFORE asking for WiFi credentials, warn the user:**

> ⚠️ WiFi 限制：
> - ESP32-C3 只支援 **2.4GHz WiFi**（5GHz 會失敗，出現 DHCP_TIMEOUT）
> - 設備和電腦必須在**同一個區網**才能通訊
> - 如果路由器 2.4G/5G 共用 SSID，可能需要分開或用手機熱點測試

Ask for SSID and password, then run everything in one python script:

```bash
python3 << 'PYEOF'
import serial, time

PORT = "SERIAL_PORT_HERE"
SSID = "SSID_HERE"
PASS = "PASS_HERE"

ser = serial.Serial(PORT, 115200, timeout=30)
time.sleep(3)  # wait for ESP32 boot after reset
ser.reset_input_buffer()

def send_and_read(cmd):
    ser.write((cmd + '\n').encode())
    resp = ''
    while True:
        line = ser.readline().decode('utf-8', errors='replace')
        if '---END---' in line: break
        resp += line
        if not line: break
    return resp.strip().split('\n')[-1]

# 1. Read info
ser.write(b'GET /info\n')
info = ''
while True:
    line = ser.readline().decode('utf-8', errors='replace')
    if '---END---' in line: break
    info += line
    if not line: break
print('DEVICE:', info.split('\n')[0] if info else 'unknown')

# 2. Connect WiFi
import json
wifi_cmd = 'POST /wifi ' + json.dumps({"ssid": SSID, "pass": PASS})
result = send_and_read(wifi_cmd)
print('WIFI:', result)

if result.startswith('OK'):
    # 3. Provision token
    time.sleep(0.5)
    token_result = send_and_read('POST /provision')
    print('TOKEN:', token_result)

ser.close()
PYEOF
```

Handle WiFi errors:
- `OK <ip>` → continue
- `ERR:SSID_NOT_FOUND` → "找不到這個 WiFi，確認 SSID 是否正確"
- `ERR:AUTH_FAILED` → "WiFi 密碼錯誤"
- `ERR:DHCP_TIMEOUT` → "拿不到 IP。最常見原因是這是 5GHz SSID。ESP32-C3 只支援 2.4GHz。試試其他 WiFi 或手機開 2.4GHz 熱點。"

## Step 4: Verify HTTP and save config

Verify HTTP connectivity:
```bash
curl -s -m 3 "http://<ip>/status"
```

If curl fails, device and computer are not on the same network.

Save device config:
```bash
mkdir -p ~/.monet
cat > ~/.monet/devices.json << 'DEVEOF'
{
  "devices": [
    {
      "name": "desk-display",
      "ip": "<IP>",
      "token": "<TOKEN>",
      "screen": <screen object from /status>,
      "device_info": "<device name from /status>",
      "serial_port": "<PORT>",
      "added_at": "<TODAY>"
    }
  ],
  "active": "desk-display"
}
DEVEOF
chmod 600 ~/.monet/devices.json
```

## Step 5: Inject statusLine push

Wire the OLED to auto-update on every Claude Code response.

### 5a. Check current statusLine config

```bash
cat ~/.claude/settings.json 2>/dev/null | jq '.statusLine'
```

Read the statusLine command script file (the `command` field points to it).

### 5b. Check if Water Lilies push is already injected

```bash
grep -q "Water Lilies push" <statusline-script-path>
```

If already present, skip to Step 6.

### 5c. Append push block to existing statusLine script

**IMPORTANT: Do NOT replace the existing statusLine. APPEND to it.**

The existing script must already extract rate limit values from the input JSON. Find the variable names it uses for:
- `.rate_limits.five_hour.used_percentage` (commonly `five_pct` or similar)
- `.rate_limits.seven_day.used_percentage` (commonly `week_pct` or similar)

Append this block at the end of the existing statusLine script, using the correct variable names:

```bash

# --- Water Lilies push (WiFi only) ---
MONET_CONFIG="${HOME}/.monet/devices.json"
if [ -f "$MONET_CONFIG" ] && [ -n "$five_pct" ]; then
  WL_IP=$(jq -r '.devices[0].ip // empty' "$MONET_CONFIG" 2>/dev/null)
  WL_TOKEN=$(jq -r '.devices[0].token // empty' "$MONET_CONFIG" 2>/dev/null)
  if [ -n "$WL_IP" ] && [ -n "$WL_TOKEN" ]; then
    WL_5H=$(printf '%.0f' "$five_pct")
    WL_7D=$(printf '%.0f' "${week_pct:-0}")
    curl -s -m 1 -X POST "http://${WL_IP}/display" \
      -H "Authorization: Bearer ${WL_TOKEN}" \
      -H "Content-Type: application/json" \
      -d "{\"draw\":[{\"type\":\"text\",\"x\":0,\"y\":13,\"text\":\"Current Session\",\"size\":10},{\"type\":\"text\",\"x\":128,\"y\":13,\"text\":\"${WL_5H}%\",\"size\":10,\"align\":\"right\"},{\"type\":\"bar\",\"x\":0,\"y\":16,\"w\":128,\"h\":12,\"pct\":${WL_5H}},{\"type\":\"text\",\"x\":0,\"y\":45,\"text\":\"Weekly Limit\",\"size\":10},{\"type\":\"text\",\"x\":128,\"y\":45,\"text\":\"${WL_7D}%\",\"size\":10,\"align\":\"right\"},{\"type\":\"bar\",\"x\":0,\"y\":48,\"w\":128,\"h\":12,\"pct\":${WL_7D}}]}" &>/dev/null &
  fi
fi
```

### 5d. If no statusLine script exists

If the user has no statusLine configured, create one:

```bash
cat > ~/.monet/statusline.sh << 'SLEOF'
#!/usr/bin/env bash
input=$(cat)

five_pct=$(echo "$input" | jq -r '.rate_limits.five_hour.used_percentage // empty')
week_pct=$(echo "$input" | jq -r '.rate_limits.seven_day.used_percentage // empty')

# Terminal output
[ -n "$five_pct" ] && printf "5h: %.0f%%" "$five_pct"

# --- Water Lilies push (WiFi only) ---
MONET_CONFIG="${HOME}/.monet/devices.json"
if [ -f "$MONET_CONFIG" ] && [ -n "$five_pct" ]; then
  WL_IP=$(jq -r '.devices[0].ip // empty' "$MONET_CONFIG" 2>/dev/null)
  WL_TOKEN=$(jq -r '.devices[0].token // empty' "$MONET_CONFIG" 2>/dev/null)
  if [ -n "$WL_IP" ] && [ -n "$WL_TOKEN" ]; then
    WL_5H=$(printf '%.0f' "$five_pct")
    WL_7D=$(printf '%.0f' "${week_pct:-0}")
    curl -s -m 1 -X POST "http://${WL_IP}/display" \
      -H "Authorization: Bearer ${WL_TOKEN}" \
      -H "Content-Type: application/json" \
      -d "{\"draw\":[{\"type\":\"text\",\"x\":0,\"y\":13,\"text\":\"Current Session\",\"size\":10},{\"type\":\"text\",\"x\":128,\"y\":13,\"text\":\"${WL_5H}%\",\"size\":10,\"align\":\"right\"},{\"type\":\"bar\",\"x\":0,\"y\":16,\"w\":128,\"h\":12,\"pct\":${WL_5H}},{\"type\":\"text\",\"x\":0,\"y\":45,\"text\":\"Weekly Limit\",\"size\":10},{\"type\":\"text\",\"x\":128,\"y\":45,\"text\":\"${WL_7D}%\",\"size\":10,\"align\":\"right\"},{\"type\":\"bar\",\"x\":0,\"y\":48,\"w\":128,\"h\":12,\"pct\":${WL_7D}}]}" &>/dev/null &
  fi
fi
SLEOF
chmod +x ~/.monet/statusline.sh
```

Then tell the user to configure Claude Code statusLine:
> 在 Claude Code 設定 statusLine command 為：`bash ~/.monet/statusline.sh`
> 可以用 `/statusline` 指令設定。

## Step 6: Test and confirm

Push a test frame:

```bash
curl -s -m 3 -X POST "http://<ip>/display" \
  -H "Authorization: Bearer <token>" \
  -H "Content-Type: application/json" \
  -d '{"draw":[{"type":"text","x":0,"y":13,"text":"Current Session","size":10},{"type":"text","x":128,"y":13,"text":"42%","size":10,"align":"right"},{"type":"bar","x":0,"y":16,"w":128,"h":12,"pct":42},{"type":"text","x":0,"y":45,"text":"Weekly Limit","size":10},{"type":"text","x":128,"y":45,"text":"18%","size":10,"align":"right"},{"type":"bar","x":0,"y":48,"w":128,"h":12,"pct":18}]}'
```

Confirm OLED shows two bars, then:

> ✓ Water Lilies 設定完成！
> - 設備 IP: <ip>
> - 螢幕自動顯示 Claude Code rate limit
> - Current Session = 5 小時滑動窗口用量
> - Weekly Limit = 7 天用量
> - 每次 Claude Code 回應後自動更新
> - 重開機會自動重連 WiFi，不需要重新設定

## Error recovery

- **WiFi `ERR:DHCP_TIMEOUT`** → 最常見是 5GHz SSID。ESP32-C3 只支援 2.4GHz。
- **curl timeout** → 設備和電腦不在同一個網段。
- **`ERR:UNAUTHORIZED`** → token 不匹配。用 pyserial 重新 `POST /provision`。
- **Serial 每次都 reset** → 正常行為（ESP32-C3 USB-CDC 限制）。只在 provisioning 用 serial，之後全走 WiFi。
- **IP 變了** → 路由器重分配 DHCP。重新 provisioning 或在路由器設定固定 IP。
