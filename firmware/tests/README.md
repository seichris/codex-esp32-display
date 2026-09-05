# Host UI regression tests

These tests compile the production `attention_ui.c` against LVGL 9.5.0. NVS is
stubbed; the layout, widgets and scrolling run in LVGL. No hardware, Wi-Fi
configuration, bearer token or physical display is needed.

After an ESP-IDF build has downloaded the managed dependencies, run from the
repository root:

```sh
cmake -S firmware/tests -B firmware/build/host-tests
cmake --build firmware/build/host-tests --parallel 8
ctest --test-dir firmware/build/host-tests --output-on-failure
```

For a separate LVGL 9.5.0 source checkout, pass
`-DLVGL_SOURCE_DIR=/absolute/path/to/lvgl` during configuration. Use a host C
compiler and CMake, outside the ESP-IDF environment. CI runs these tests on
Linux in addition to the ESP-IDF 5.4.4 firmware build.

Coverage includes viewport height and card bounds at 410×502, fixed bottom
position during selection/scrolling, scroll preservation through refreshes,
shrinking lists, all title font sizes, long titles, empty state, a resized
viewport, and controller-unavailable/unknown/inferred/confirmed status.

At PR #4's original `b9d2955`, the host reproduced scroll reset during refresh
and the misleading empty-target message. The initial 410×502 layout measured a
306-pixel list; the exact reported physical “tiny sliver” symptom was **not**
reproduced. The new flex budget measures 309 pixels and keeps the 112-pixel
bottom card fixed, with assertions covering the geometry. These tests do not
establish physical rendering, touch behavior, Desktop selection observation,
or Voice operation on the Waveshare device. Physical verification remains
outstanding and requires a separately authorized flash.
