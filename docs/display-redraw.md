# Display redraw implementation

The device reports a scrollable sliver above the fixed Voice card and stale
detail text after returning to the list. The earlier viewport-only change did
not resolve those physical symptoms. Host layout and rendering tests produced
correct images, so they did not establish the cause on the panel.

The replacement display path keeps the Waveshare BSP's panel initialization,
power, brightness and touch setup, but owns frame delivery:

- LVGL renders a complete 410×502 RGB565 image in 411,640 bytes of PSRAM.
- A permanent 16,400-byte internal DMA buffer is allocated before audio and
  Wi-Fi start. Each transfer copies at most 20 full-width rows into it, with
  the panel's RGB565 byte order. The last strip is two rows.
- QSPI address and pixel commands follow the board BSP's SH8601-compatible
  initialization and 22-column panel offset. Every command result is checked.
- The staging buffer cannot be overwritten until the asynchronous completion
  callback arrives, including after a timeout. LVGL's full-frame buffer is
  separate and can safely be released while staging remains in flight.
- A failed frame schedules a complete redraw of the currently visible screen.
  It does not mark a partial transfer successful or retain a stale image as
  the desired display contents.
- List, detail and settings roots have opaque backgrounds. View transitions
  explicitly invalidate the active screen, so returning to the list clears
  detail text across the whole display.

This removes dependence on dirty-rectangle delivery and temporary per-transfer
DMA staging allocations. The tradeoff is more panel bandwidth and roughly
402 KiB for the complete image in PSRAM. Scrolling remains supported, but its
physical frame rate is not yet measured.

## Why the managed flush path was replaced

In the installed Waveshare SH8601 driver, `panel_sh8601_draw_bitmap` ignores
`tx_color`'s return value. The LVGL port also ignores the panel draw result.
Those boundaries cannot reliably distinguish a delivered frame from a failed
transfer. The BSP's `start_with_config` entry point additionally ignores its
caller-provided DMA/buffer flags when constructing its display configuration.
The replacement uses the BSP's lower-level public panel/touch initialization
APIs and keeps all new transport code in this repository, rather than editing
managed dependencies or passing ignored configuration flags. The board BSP is
pinned to 2.0.0 so its initialization stays matched to the transport commands;
future BSP upgrades must review that contract.

These source findings are not a claim that a particular DMA failure was
observed on the user's device. The transport and redraw contracts are now
explicit and tested; physical resolution of the reported symptom is still an
acceptance check, not established by a successful build.

## Verification

- The real LVGL host renderer drives the production strip sender into a
  simulated panel and compares the entire list image before/after details and
  settings, including its background. Text pixels occupy the area above the
  Voice card.
- Transfer tests cover all 502 rows, byte order, strip bounds, failure halfway
  through a frame, complete recovery, and delayed completion across retries.
- The existing geometry, selection, fonts, scroll and status tests remain.
- Build with ESP-IDF 5.4.4. No physical flash is authorized by these tests.

Chris explicitly requires a fresh confirmation before flashing this build.
