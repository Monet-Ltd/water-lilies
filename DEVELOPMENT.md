# Water Lilies — Development Guide

## Hardware

- **MCU**: ESP32-C3 Super Mini (RISC-V 160MHz, 400KB SRAM, 4MB Flash, Native USB-CDC)
- **Display**: SSD1306 128x64 Monochrome OLED (I2C)
- **Wiring**: GPIO5=SCL, GPIO6=SDA, 3.3V, GND

## Toolchain

```bash
# Arduino CLI (macOS)
brew install arduino-cli

# ESP32 core
arduino-cli core install esp32:esp32

# Libraries
arduino-cli lib install "U8g2" "ArduinoJson"
# WebServer is bundled with ESP32 core, no separate install needed
```

## Build & Flash

```bash
cd firmware/water-lilies-mvp

# Compile
arduino-cli compile --fqbn esp32:esp32:esp32c3 \
  --build-property "build.extra_flags=-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1" .

# Upload (check port with: ls /dev/cu.usb*)
arduino-cli upload --fqbn esp32:esp32:esp32c3 --port /dev/cu.usbmodem101 .
```

## Serial Communication

```bash
# Quick test (send command, read response)
echo 'GET /info' > /dev/cu.usbmodem101 && sleep 2 && timeout 3 cat /dev/cu.usbmodem101

# Draw something
echo 'POST /display {"draw":[{"type":"text","x":0,"y":12,"text":"hello","size":12}]}' > /dev/cu.usbmodem101
```

For reliable serial communication, use pyserial:

```bash
pip3 install pyserial

python3 -c "
import serial, time
ser = serial.Serial('/dev/cu.usbmodem101', 115200, timeout=5)
time.sleep(0.5)
ser.write(b'GET /info\n')
resp = ''
while True:
    line = ser.readline().decode('utf-8', errors='replace')
    if '---END---' in line: break
    resp += line
    if not line: break
print(resp)
ser.close()
"
```

## Known Issues & Solutions

### OLED flicker on repeated updates

Pushing the same data repeatedly causes visible flicker from `clearBuffer` + `sendBuffer`.

**Fix (two layers):**
1. **Client side**: Cache last pushed values in `~/.monet/.last_push`. Skip curl if unchanged.
2. **Firmware side**: Hash incoming JSON payload. Skip redraw if hash matches last frame.

### statusLine background process gets killed

`curl ... &>/dev/null &` (background) gets killed when Claude Code's statusLine script exits.

**Fix:** Run curl in foreground with `-m 1` (1 second timeout). The cache layer prevents unnecessary calls, so the blocking curl rarely fires.

### NVS "NOT_FOUND" error on first boot

```
[843][E][Preferences.cpp:47] begin(): nvs_open failed: NOT_FO
```

**Not a bug.** NVS partition is uninitialized on first flash. Goes away after first write (e.g., `POST /wifi`).

### WiFi "sta is connecting, cannot set config"

```
E (278610) wifi:sta is connecting, cannot set config
```

**Cause:** Sending `POST /wifi` while a previous connection attempt is still active.
**Fix:** Call `WiFi.disconnect(true)` + `delay(500)` before `WiFi.begin()`. Applied in v0.1.1.

### DHCP Timeout with 5GHz SSID

ESP32-C3 only supports **2.4GHz WiFi**. SSIDs ending in `_5G` or dual-band SSIDs that steer to 5GHz will fail with `ERR:DHCP_TIMEOUT` (not `ERR:SSID_NOT_FOUND` — the AP is visible but the C3 can't associate properly).

**Fix:** Use a 2.4GHz-only SSID.

### ESP32-C3 resets on every serial port open (USB-CDC limitation)

Every time a program opens `/dev/cu.usbmodem101`, the ESP32-C3 performs `USB_UART_CHIP_RESET` and reboots. This is a **hardware limitation** of the native USB-CDC controller — unlike CH340-based boards where USB-UART is a separate chip.

```
CH340 boards: USB open/close → CH340 handles it → ESP32 unaffected
USB-CDC (C3): USB open/close → ESP32 USB controller → chip reset
```

Attempts that **don't work**:
- `stty -hupcl` — only prevents reset on close, not on open
- `dsrdtr=False, rtscts=False` in pyserial — USB-CDC ignores these
- Keeping serial port open with a daemon — works but complex

**Conclusion: Serial cannot be used for ongoing data push. Use WiFi HTTP instead.** Serial is only for one-time provisioning (where the reset is acceptable).

### Serial buffer truncation (large JSON payloads)

Arduino's `Serial.readStringUntil('\n')` has a small internal buffer (~256 bytes). JSON payloads >256 bytes get truncated, causing `ERR:INVALID_JSON IncompleteInput`.

**Fix:** Replaced with manual byte-by-byte read loop with 5s timeout (applied in firmware). When sending via pyserial, use 64-byte chunks with 20ms delays between chunks.

### Serial output garbled or empty

The ESP32-C3 USB Serial/JTAG can have brief dropouts during WiFi events. This is a known chip-level issue ([esp-idf#8046](https://github.com/espressif/esp-idf/issues/8046)).

**Workaround:** Retry the serial read. For provisioning this is fine — WiFi is off during serial setup.

### `echo` + `cat` timing issues

Using `echo > /dev/cu.usbmodem101 && cat /dev/cu.usbmodem101` is unreliable — the response may arrive before `cat` starts, or `cat` may read stale data.

**Workaround:** Add `sleep` between write and read, or use pyserial for reliable framing.

### `collectHeaders` API difference

ESP32 Arduino Core 3.x changed the `WebServer::collectHeaders` signature:

```cpp
// Wrong (compiles on older cores):
server.collectHeaders("Authorization");

// Correct (ESP32 Arduino Core 3.x):
const char* headerKeys[] = {"Authorization"};
server.collectHeaders(headerKeys, 1);
```

## Flash/RAM Budget

| Resource | Used | Total | Remaining |
|----------|------|-------|-----------|
| Flash | 83% (1.1MB) | 1.3MB | 215KB |
| RAM (global) | 12% (39KB) | 328KB | 288KB |

At runtime with WiFi active, free heap is ~150-200KB. Enough for HTTP server + JSON parsing + display rendering.

## Port Detection

```bash
# macOS
ls /dev/cu.usb*

# Linux
ls /dev/ttyUSB* /dev/ttyACM*

# The ESP32-C3 Super Mini (native USB) typically shows as:
#   macOS:  /dev/cu.usbmodem101 or /dev/cu.usbmodem1101
#   Linux:  /dev/ttyACM0
```
