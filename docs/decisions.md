# Decisions

Newest first. Records *why*, so a later reader does not undo a choice without
knowing what it cost.

---

## Ported to the Elecrow CrowPanel Advanced 10.1" (EK79007, 1024x600)

What changed and why it is not a find-and-replace:

- **The panel is landscape-native.** Orientation used to mean "landscape =
  rotate 90". It now means "rotate when the view disagrees with the panel"
  (`display_view_rotated()`), so landscape is free on this board and portrait
  is the rotated mode. The touch inverse-mapping rows were measured on the old
  board in panel terms; they should carry over, but check a portrait swipe.
- **Touch is a GT911**, a register-only part, replacing the GSL3680 and its
  88 KB firmware upload. The address is latched at reset, so both are tried.
- **SD is 1-bit.** Only D0 is routed. Its rail is internal LDO 4, which the
  display now holds at a fixed 3.3 V (Elecrow does the same), so the SD driver
  no longer asks for it as an adjustable supply.
- **esp_hosted needs `CONFIG_ESP_HOSTED_P4_DEV_BOARD_NONE`.** The default
  board profile range-locks every SDIO pin to Espressif's dev board. The
  CrowPanel reverses D0..D3 (17..14), and Kconfig silently dropped those
  values with only a build warning until the profile was changed.
- **esp_hosted is pinned to 2.12.3**, the version the C6 ships with. 3.x
  links up over SDIO but cannot complete RPC bring-up with a 2.x C6, and
  3.1.0 aborts outright when its 30 KB RX preallocation does not fit in
  internal DMA RAM. Floating versions are what exposed this: deleting the lock
  file during the port pulled in 3.1.0. Going to 3.x means reflashing the C6.
- **The C6 reset is GPIO 32, not Elecrow's 54** -- measured. With 54 the C6 is
  never reset and a 2.x host waits forever for its boot-up event.
- **Portrait rail is 600 px wide here, not 800.** Cell widths now scale with
  the rail, but the right cell's content wants ~262 px and gets ~238. Portrait
  will clip until it gets a narrower design; landscape is unaffected.


## ESP-IDF, not Arduino — and why that is not a preference

Arduino-ESP32 has no MIPI-DSI driver for the P4. Beyond that, the hardware JPEG
codec and the PPA blitter are IDF-only, and those are precisely what a 1280×800
photo frame lives on. The CYD project is Arduino and that experience carries
over at the application layer, but the platform does not.

Pinned to **v5.5.5**: the Waveshare and Espressif BSP components track 5.4/5.5,
and 6.x brings API churn this project gains nothing from.

## Photos are cached fitted to a square 1280×1280 box, not to screen geometry

The obvious design caches at final screen size. Caching to a square box instead
buys two things:

1. **Orientation switching is free.** The rotation happens in the single
   per-photo PPA fit pass, so flipping landscape↔portrait needs no cache rebuild.
2. **Immich skips ingest entirely.** A `size=preview` rendition is ~1440 px on
   the long edge — already inside the HW decoder's range, so it joins the
   pipeline at playback.

Cost: cache entries are slightly larger than they strictly need to be.

## The cache is not an optimisation — it is the only way this works at all

The P4's hardware JPEG decoder has no downscale-during-decode. A 12 MP photo
decodes to ~24 MB of RGB565 and cannot be handed to it. Every photo therefore
passes through an ingest transcode once, and playback only ever reads cache
entries. Without this the feature is not slow, it is impossible.

## Two SDMMC slots, no contention

The C6 radio is on slot 1 via `esp_hosted`; the microSD is on slot 0, whose
pins are fixed in silicon. On many P4 boards these contend. Here they do not,
which is why the frame can serve a web upload while playing photos.

## Settings live in NVS, set from a web page

Carried over from the CYD project unchanged, for the same two reasons:
re-flashing to change a setting is a poor repair path for something hanging on
a wall, and a binary built from a header full of secrets carries those secrets.

## The web UI gets a password; the setup AP does not

New, and a departure from CYD. This device accepts photo uploads and stores an
Immich API key, so LAN-trusted is no longer a reasonable default. The setup AP
stays open so a forgotten password is always recoverable by holding a finger on
the panel through power-on.
