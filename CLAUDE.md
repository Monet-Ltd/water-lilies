# Water Lilies — Project Context

## What is this

Agent-native IoT display by Monet Ltd. The device describes itself via `/info` endpoint in natural language — AI agents read it and know how to use the device automatically.

## Current hardware (MVP)

- ESP32-C3 Super Mini + SSD1306 128x64 I2C OLED
- GPIO5=SCL, GPIO6=SDA
- ProFont monospace font family

## Critical constraints

- **WiFi only for data push.** ESP32-C3 USB-CDC resets the chip on every serial port open. Serial is only for one-time provisioning.
- **2.4GHz WiFi only.** 5GHz SSIDs fail with DHCP_TIMEOUT.
- **No PSRAM.** Can't do 240x240 framebuffer. SSD1306 128x64 mono = 1KB, fine.

## Build

```bash
cd firmware/water-lilies-mvp
arduino-cli compile --fqbn esp32:esp32:esp32c3 \
  --build-property "build.extra_flags=-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1" .
arduino-cli upload --fqbn esp32:esp32:esp32c3 --port /dev/cu.usbmodem101 .
```

## Key files

- `firmware/water-lilies-mvp/water-lilies-mvp.ino` — all firmware in one file
- `DEVELOPMENT.md` — build guide, known issues, solutions
- `docs/agent-native-iot-display.md` — API spec and protocol
- `docs/hardware-options.md` — hardware comparison with test results

## Related repo

- Plugin: https://github.com/Monet-Ltd/monet-setup (`/monet-setup` Claude Code slash command)

## Conventions

- Commit format: conventional commits (`feat:`, `fix:`, `docs:`, `refactor:`)
- 繁體中文 for docs, English for code comments
- Firmware: Arduino framework, not ESP-IDF directly
