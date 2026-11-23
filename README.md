# ESP32-P4 10.1" Digital Photo Frame — Elecrow CrowPanel Advanced

ESP-IDF firmware that turns an **Elecrow CrowPanel Advanced 10.1" ESP32-P4 HMI
display** into a wall photo frame. It plays photos from a microSD card and from
an [Immich](https://immich.app) server, accepts uploads from a browser, and
shows a clock / weather / sun / moon / air-quality bar along the bottom of the
screen.

> **Status:** running on hardware (board rev **V1.2**). Panel, touch, microSD,
> photo playback, transitions, WiFi, the setup portal, and both landscape and
> portrait mode are verified on the device.

---

## Hardware

**Board:** Elecrow CrowPanel Advanced 10.1" ESP32-P4 HMI AI Display,
1024×600 IPS, board revision **V1.2**
([wiki](https://www.elecrow.com/wiki/CrowPanel_Advanced_10.1inch_ESP32-P4_HMI_AI_Display_1024x600_IPS.html),
[vendor examples](https://github.com/Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen)).

| Part | Detail |
|---|---|
| SoC | ESP32-P4NRW32, dual-core RISC-V @ 360 MHz, chip revision **v1.3** (pre-v3) |
| Memory | 16 MB flash, 32 MB in-package PSRAM (hex, 200 MHz) |
| Display | 10.1" IPS, **EK79007** controller, **1024×600, native landscape**, 2-lane MIPI-DSI @ 900 Mbps |
| Backlight | LEDC PWM on GPIO 31 (30 kHz), supply enable GPIO 29 |
| Touch | **GT911** capacitive, I2C (SDA 45 / SCL 46), RST 40 / INT 42, address 0x5D or 0x14 |
| Storage | microSD on SDMMC slot 0, **1-bit** (CLK 43 / CMD 44 / D0 39) |
| WiFi / BLE | **ESP32-C6** co-processor over SDIO slot 1, 4-bit, reached through `esp_hosted` |
| Audio | NS4168 I2S amplifier + PDM microphone: present, unused |

Every pin, with where each value came from, is in
[`main/board/board.h`](main/board/board.h).

### Things about this board that are not obvious

These cost real debugging time and are easy to undo by accident:

- **The C6 reset is GPIO 32, not 54.** Elecrow's own WiFi example says 54.
  With 54 the C6 is never reset, and the host waits forever for it to announce
  itself.
- **The C6's SDIO data lines are reversed** from Espressif's defaults:
  D0..D3 = GPIO 17, 16, 15, 14. `esp_hosted` range-locks these pins to
  Espressif's dev board unless `CONFIG_ESP_HOSTED_P4_DEV_BOARD_NONE=y` is set.
  Without it, Kconfig drops the values with only a build-time warning.
- **`esp_hosted` is pinned to 2.12.3** to match the firmware the C6 ships
  with. A 3.x host cannot finish setting up the link with a 2.x C6, and 3.1.0
  crashes at boot on this memory budget.
- **The microSD runs 1-bit**, because only D0 is routed. Its supply is the
  P4's internal LDO 4, which the display driver switches on, so the card is
  mounted after the display comes up.
- **The chip is a pre-v3 ESP32-P4.** The build selects
  `CONFIG_ESP32P4_SELECTS_REV_LESS_V3`; a default IDF build will not boot on it.

---

## Features

- **Photos** from `/sdcard` (any folder; `.jpg` / `.jpeg`), letterboxed to fit,
  never cropped. Up to 2048 px per side is decoded by the P4's hardware JPEG
  decoder in about 30 ms. Larger photos are decoded in software at 1/2, 1/4 or
  1/8 scale so a 12 MP phone photo still fits in memory.
- **Immich**: plays albums from your server using its ~1440 px preview
  renditions, cached on the card (`/sdcard/immich`, size limit configurable).
  It can mix with the SD photos.
- **10 hardware-accelerated transitions**: crossfade, slides, push, wipe,
  barn door, blinds and dissolve, at about 70–75 fps on this panel.
- **Info bar**: clock and date, weather (Open-Meteo), daylight arc with
  sunrise and sunset, moon phase, and US AQI with PM2.5, over two rotating
  pages. Six colour themes.
- **Landscape or portrait**, plus a 180° flip for either mounting. Landscape
  is the panel's native orientation and the default.
- **Touch**: tap for the next photo, double-tap for the previous one, swipe up
  to show the info bar and swipe down to hide it.
- **Web settings page**: WiFi (with a network scan), location, time zone,
  units, orientation, seconds per photo, theme, Immich server and albums, and an
  optional password. Upload, browse and delete photos from the browser.
- **Setup portal**: if the frame cannot reach its WiFi network, it opens an
  open access point named `PhotoFrame-XXXX` with the settings page at
  http://192.168.4.1/, so it can always be fixed without a cable.

---

## Getting started

### 1. Toolchain

- [ESP-IDF **v5.5.5**](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/get-started/),
  installed at `~/esp/esp-idf` (or set `IDF_PATH`).
- Target `esp32p4`.

Source the environment in every new shell:

```bash
source tools/env.sh
```

`tools/env.sh` runs IDF's `export.sh`. On macOS it also points `SSL_CERT_FILE`
at certifi's bundle, because the python.org Python framework ships without
OpenSSL certificates and the component manager would otherwise fail with
`CERTIFICATE_VERIFY_FAILED`.

### 2. Build and flash

Connect the board's **UART** USB port (it shows up as a CH340
`wchusbserial` device on macOS) and turn the power switch on.

```bash
idf.py build
```

```bash
idf.py -p /dev/cu.wchusbserial1440 flash monitor
```

Use your own port name. The first build downloads components (LVGL,
`esp_hosted`, the EK79007 and GT911 drivers) from the ESP component registry.

### 3. Prepare a microSD card

Format it **FAT32**; exFAT is not supported, and most cards over 32 GB come as
exFAT. Copy JPEGs anywhere on it. Uploads from the web page go into `/photos`.

### 4. First boot

1. On a fresh board the frame opens the setup network **`PhotoFrame-XXXX`**
   (the bottom bar shows the exact name).
2. Join it from a phone. The settings page should open by itself; if not,
   browse to http://192.168.4.1/.
3. Pick your WiFi network, set your location and time zone, and save. The
   frame restarts and joins your network.
4. Afterwards the bottom bar shows the settings address, for example
   `setup 192.168.1.50`.

For the time zone, use a POSIX string so daylight saving is handled, for
example `PST8PDT,M3.2.0,M11.1.0` for US Pacific.

### GUI flasher (optional)

[`gui-flasher/`](gui-flasher/) is a small point-and-click tool for flashing a
build without an IDF shell. Run `python3 gui-flasher/flasher.py`. See its
README.

---

## Project layout

```
main/
  board/          board.h (pin map), display, touch, sdcard drivers
  photo/          decode + fit (photo.c), playlist, transitions
  net/            wifi, web UI + uploads, Immich, weather, air quality, sun/moon
  ui/             info-bar overlay (LVGL), themes, icons, fonts
  settings.c      settings in NVS, set from the web page
docs/
  decisions.md    why things are the way they are, newest first
  stage0_results.md
stage0/           hardware bring-up test harness (see below)
gui-flasher/      optional GUI flashing tool
tools/env.sh      sources ESP-IDF
sdkconfig.defaults  all build configuration, commented
partitions.csv    nvs + 6 MB factory app, no OTA
```

## How it works, briefly

- The display runs two framebuffers in PSRAM. The compositor draws the next
  frame into one while the panel scans the other, so transitions do not tear.
- **One rotation, once per photo.** Each photo is scaled, letterboxed and
  rotated in a single pass of the P4's 2D accelerator (PPA) when it loads.
  Switching orientation never costs anything per frame.
- `display_view_rotated()` is the single place that decides whether the view
  is turned against the panel. Photos, the info bar and the touch mapping all
  ask it, so they cannot disagree.
- The info bar is drawn by LVGL into its own translucent layer, which the PPA
  blends over each frame. A built-in fit check logs any element that spills
  outside its cell, so layout problems show up in the serial log.
- `esp_hosted` buffers larger than 2 KB are allocated from PSRAM rather than
  scarce internal DMA RAM. Without this, a WiFi scan result crashes the frame.

See [`docs/decisions.md`](docs/decisions.md) for the reasoning behind each of
these.

## Development notes

- **`stage0/`** is a standalone IDF project of hardware "gates" (backlight,
  display, touch, SD, PPA, JPEG). It was used to bring up the firmware's
  **previous** board, a Guition JC8012P4A1, and **has not been ported** to the
  CrowPanel yet.
- This firmware was first written for that Guition board and then ported. The
  history is in the git log and in `docs/decisions.md`.

## Credits

- Pin and panel timing values come from Elecrow's
  [CrowPanel Advanced 10.1" examples](https://github.com/Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen).
- The C6 reset pin and data-line order were confirmed against a community
  ESPHome configuration
  ([MaxGramser/homeassistant_espscreen](https://github.com/MaxGramser/homeassistant_espscreen/blob/main/docs/CROWPANEL101.md)).
- Weather and air quality: [Open-Meteo](https://open-meteo.com).
- Font: IBM Plex Sans Condensed (SIL Open Font License).
