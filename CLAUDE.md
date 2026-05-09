# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

ESP8266 (Wemos D1 mini) firmware that bridges an RS485/UART bus to a browser via WebSocket. The device serves a web UI (HEX/ASCII monitor + preset packet sender) and supports OTA updates. Built with PlatformIO + Arduino framework.

## Build / flash / run

PlatformIO is the only build system; there is no make/cmake. Two environments are defined in [platformio.ini](platformio.ini):

- `d1_mini_lite` — USB serial upload (default for first flash)
- `d1_mini_lite_ota` — espota upload, hardcoded to `upload_port = 10.11.85.67` and password `admin`. Update the IP in `platformio.ini` to match the device's current STA IP before using.

Common commands (run from repo root):

```bash
pio run -e d1_mini_lite                       # build firmware
pio run -e d1_mini_lite -t upload             # flash via USB
pio run -e d1_mini_lite_ota -t upload         # flash via OTA
pio run -e d1_mini_lite -t uploadfs           # upload data/ to SPIFFS (required after web UI changes)
pio device monitor                            # serial monitor at 9600 baud
```

The `data/` directory (HTML/CSS/favicon/preset.ini) is served from SPIFFS — code-only changes do **not** require `uploadfs`, but any edit under `data/` does.

## Architecture

All firmware logic lives in a single file: [src/main.cpp](src/main.cpp). The flow:

1. `setup()` mounts SPIFFS, registers handlers, then calls `setupCONNECTION()`.
2. `setupCONNECTION()` reads `/config.ini` (format: `ssid;password`). On missing file or 15-attempt connect failure, falls back to AP mode (SSID `everyESP`, IP `192.168.4.1`).
3. `AsyncWebServer` on port 80 serves static files from SPIFFS root (default `index.htm`) plus these endpoints (full list in [README.md](README.md)): `/ws` (binary WebSocket), `/events` (SSE for OTA progress), `/api/status`, `/scan` + `/wifistatus`, `/connect2ssid`, `/wifireset`, `/savepreset`. JSON request bodies are parsed with ArduinoJson.
4. `loop()` reads from `Serial` non-blocking into an 8 KB buffer; flushes to all WS clients via `ws.binaryAll()` when an idle gap of `PACK_TIMEOUT_US` (5 ms) is seen or the buffer fills. Before sending, free heap is checked against `WS_HEAP_GUARD` (8 KB) and the packet is dropped (`wsBinDropped++`) if the device is heap-pressured. Inbound WS frames are reassembled in `wsRxBuf` (8 KB) across fragments and written to `Serial` only on the final fragment so the RS485 packet boundary is preserved.
5. mDNS + `ArduinoOTA` are only initialized when STA connects successfully. OTA password is `admin`.

## Critical constraints (do not regress)

- **RS485 bus cleanliness**: `RS485_CLEAN_BUS=1` near the top of [src/main.cpp](src/main.cpp) disables all debug output via the `DBG_PRINT*` macros, because the same `Serial` is the RS485 TX line — any stray `Serial.print` corrupts the bus. Never use raw `Serial.print*` for logging; use `DBG_PRINT*` macros so `RS485_CLEAN_BUS` can suppress them. The intentional raw `Serial.write(...)` calls in `webSocketEvent` (single-frame and reassembled-fragment paths) are the bridge path and must stay.
- **WS fragment reassembly**: WS → RS485 path must NOT call `Serial.write` per fragment. The fragmented branch in `webSocketEvent` accumulates into `wsRxBuf` and writes once when `info->final && index+len == info->len`. Writing per fragment splits a single logical packet into multiple RS485 frames and breaks bus-level framing on the slave side.
- **No blocking `delay()` in the RS485 read path**. `loop()` reads available bytes non-blocking and uses `micros() - lastByteUs >= PACK_TIMEOUT_US` to detect packet end. Earlier revisions used `delay(packTimeout)` inside an inner while-loop and starved WS/OTA handling under continuous traffic.
- **WebSocket ping is intentionally disabled** in `webSocketEvent` (`WS_EVT_CONNECT` branch). Calling `client->ping()` on connect caused resets in this stack (ESPAsyncTCP-esphome on ESP8266). Don't re-enable without verifying stability.
- **WS stats log interval is 5 s**, not 1 s. Tightening it caused stability issues under heavy RS485 load. The 5 s log line now also reports `dropped` and `heap` for backpressure visibility.
- **Serial baud is 9600** (`monitor_speed` in `platformio.ini` and `Serial.begin` in `main.cpp`). `Serial.setRxBufferSize(1024)` is called before `Serial.begin` to absorb RS485 burst.
- **`credentials.h` is committed with stub values** ([src/credentials.h](src/credentials.h)) and is **not** gitignored. It defines `USER_NAME` / `USER_PASSWD` for the `SPIFFSEditor` admin login only — no WiFi creds are baked in (those live in SPIFFS `/config.ini`). Avoid committing real credentials here.

## Persisted files on SPIFFS

- `/config.ini` — `ssid;password` (written by `/connect2ssid`, deleted by `/wifireset`)
- `/preset.ini` — JSON preset packets (written by `/savepreset`, also shipped in `data/` as a default). Schema v1: `{"version":1,"data":[{"name":"…","hex":"…","note":"…"}, ...]}`. The web UI also accepts the legacy `{"data":["hex string", ...]}` form and migrates on save.
- All files under `data/` — served as static assets

## Testing

There is no unit test suite. Manual integration testing only:

```bash
pip3 install pyserial   # one-time
cd test && python3 test_serial.py   # interactive: 5 packet-size modes, see README §시리얼 데이터 전송 테스트
```

Edit `PORT` and `BAUD` at the top of [test/test_serial.py](test/test_serial.py) before running.
