# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DIY E-Ink photo album: ESP32 (S3 or C3 Super Mini) drives a 7.3" 7-color (AC073TC1) e-paper display. Images are converted to 4bpp raw bitmap format via `show.py` and sent to the ESP32 over HTTP. The ESP32 publishes device status via MQTT (HomeAssistant auto-discovery).

## Build / Flash

```bash
# ESP32-S3 (default)
pio run -e esp32s3 -t upload

# ESP32-C3 Super Mini
pio run -e esp32-c3-super-mini -t upload

# Monitor
pio device monitor -b 115200
```

PlatformIO is required. Dependencies: FastLED, ArduinoJson, PubSubClient.

A `key.hpp` file (gitignored) provides default WiFi credentials via `KEY_WIFI_SSID` / `KEY_WIFI_PASSWORD` macros. `scripts/proxy_setup.py` injects an HTTP proxy for dependency downloads.

## Architecture

### ESP32 Firmware (`src/`)

**`main.cpp`** — Entry point. `setup()`: init indicator LED → init EPD → load config from NVS → connect WiFi → start HTTP server + MQTT. `loop()`: handle HTTP client, WiFi reconnection, MQTT reconnect + polling, and deferred image flush (`image_uploaded` flag).

**`epd_7in3e.hpp/.cpp`** — EPD panel driver. Key facts:
- Panel: 800×480 pixels, 7 colors, **4 bits per pixel** packed as 2 pixels per byte (high nibble = even pixel, low nibble = odd pixel)
- `BUFFER_SIZE` = 192000 bytes (`WIDTH * HEIGHT / 2`)
- Writing an image: `buffer[]` is filled via `set_pixel()`, then `flush_buffer()` sends it over SPI
- Color values: 0=BLACK(0x00), 1=WHITE(0x01), 2=YELLOW(0x02), 3=RED(0x03), 4=ORANGE(0x04), 5=BLUE(0x05), 6=GREEN(0x06)
- The `canvas_interface` base class just defines `set_pixel(x, y, color)` — not heavily used

**`http_server.hpp/.cpp`** — HTTP API on port 80:
- `GET /` — Config web UI (HTML form)
- `GET /api/config` — Return current config as JSON
- `POST /api/config` — Save config, trigger MQTT reconnect
- `POST /api/upload` — Receive base64-encoded 4bpp raw data, decode to `epd.buffer`, set `image_uploaded = true`
- `POST /api/reboot` — `ESP.restart()`
- `GET /api/health` — Uptime in microseconds

**`mqtt_ha.hpp/.cpp`** — MQTT with HomeAssistant auto-discovery. Publishes two diagnostic sensors (`sensor/<device>_status` and `sensor/<device>_ip`). Subscribes to `sensor/<device>/refresh/set` to receive refresh commands. Uses PubSubClient over WiFiClient.

**`config.hpp/.cpp`** — Configuration stored in ESP32 NVS (Preferences library). Fields: wifi_ssid, wifi_password, mqtt_broker, mqtt_port, mqtt_user, mqtt_password, ha_device_name. Defaults fall back to `key.hpp` macros.

**`hardware_define.h`** — Pin definitions, switched by compile-time defines: `CONFIG_PROTOTYPE_C3_SUPER_MINI` vs `CONFIG_PROTOTYPE_S3`.

**`indicator.hpp/.cpp`** — WS2812 LED status indicator (S3 only, gated by `#ifdef LED_WS2812_CNT`). Color codes: Red=Busy, Blue=WiFi connecting, Cyan=WiFi connected, Yellow=Updating, Dim Purple=Idle.

**`log.hpp`** — `LOG(fmt, ...)` macro wrapping `Serial.printf()` with timestamp and file:line.

### Python CLI (`show.py`)

Standalone script for image conversion and sending. Supports `--mode http` (direct to ESP32), `--mode direct` (ESPHome native API), `--mode ha` (HA REST API).

Image pipeline: PIL open → rotate (optional) → crop to 5:3 → resize to 800×480 → Floyd-Steinberg dither to 7-color palette → nearest-color mapping → pack into 4bpp bytes → send via chosen mode.

## Data Flow

```
show.py (or any client)
  → convert image to 4bpp (192000 bytes)
  → base64 encode
  → HTTP POST to ESP32 /api/upload
  → ESP32: base64 decode → epd.buffer[] → set image_uploaded=true
  → loop(): flush_buffer() → SPI send to EPD → deep sleep panel
  → MQTT: publish status "idle" → "updating" → "done" → "idle"
```

## Key Constraints

- The EPD buffer is exactly 192000 bytes. The ESP32 HTTP handler rejects any upload that decodes to a different size.
- Color indices in `show.py` must match the hardware `COLOR_VALUE` table in `epd_7in3e.cpp` (0=Black, 1=White, 2=Yellow, 3=Red, 4=Orange, 5=Blue, 6=Green).
- MQTT max packet size is set to 512 bytes (build flag `-D MQTT_MAX_PACKET_SIZE=512`).
- The S3 build enables PSRAM with 16MB flash partition.
