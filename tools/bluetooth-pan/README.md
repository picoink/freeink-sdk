# ESP32-S31 PAN libraries

The Arduino 4.0.0-RC1 binary libraries use NimBLE and do not export Classic PAN.
`build.py` builds an isolated replacement for the **ESP32-S31, QIO/OPI** target.
It preserves Arduino 4.0 RC1 / ESP-IDF 6.1 and USB MSC. It does not upgrade the
application to an unreleased IDF 6.2 framework or modify system PlatformIO packages.

From the firmware project's root, with Python 3.12+, Git, curl and build tools:

```sh
python3 freeink-sdk/tools/bluetooth-pan/build.py
```

The first build downloads the toolchain, sources and IDF managed components and
requires several GB. Defaults are `.tools/pan-platformio` for the isolated Core
and packages, `.tools/pan-build` for the build, and `.tools/bluetooth-pan-libs` for
the output. Paths can be changed with the corresponding command options. Keep
these generated files outside source control. Rerun with unchanged inputs to
reuse downloads and compiled libraries; use new build/core directories when
changing the framework baseline. `--export-only` re-exports existing compiled
libraries and does not run a compilation check.

The firmware environment must select the resulting package and opt in:

```ini
platform_packages = framework-arduinoespressif32-libs @ symlink://.tools/bluetooth-pan-libs
build_flags = -DFREEINK_CAP_BLUETOOTH_PAN=1
```

Merge these entries with the environment's existing flags. Other targets keep
the flag unset and use their regular libraries. A supported SoC alone is not
sufficient: the capability API also checks the host stack and PAN configuration.

## Source provenance

- ESP-IDF base: `94639ab7251fc8230ca725df638c9a57dbb17153`, the exact
  revision recorded by the Arduino libraries' `versions.txt`. The pioarduino
  `esp-idf-v6.1.0` package is only a bootstrap and contains a different revision.
  The builder reconstructs IDF sources from the pinned Git tree, verifies every
  file's Git blob hash and restores the S31 dependencies in `submodules.json`,
  checking their revisions against the same IDF tree.
  In particular, the CPU protection startup code and controller archives must
  match this baseline; a successful link alone cannot detect a mismatch.
- Arduino core/libraries: `4.0.0-RC1` (`9f21380ff` core).
- PAN source snapshot: Espressif ESP-IDF
  [`ad20ded62a2d67a26e501fdc1905f43ba7ad6c81`](https://github.com/espressif/esp-idf/tree/ad20ded62a2d67a26e501fdc1905f43ba7ad6c81/components/bt/host/bluedroid).
  Shared-file integration comes from the initial PAN addition
  `4f54340e51f391bb058b220d8a40f741319c909c` to avoid unrelated IDF changes.
- The patch includes the newer PAN/BNEP fixes, with `ESP_PAN_SEC_*` expressed as
  their equivalent 6.1 BTM masks (`0x0012`, `0x0024`), since the shared public
  `ESP_BT_SEC_*` names were added after this baseline. Apache-2.0 notices remain
  in the upstream files; ESP-IDF's license continues to apply. The integration
  preserves the baseline's existing PBAP power-management table entries.
- Arduino TinyUSB component: lib-builder
  `8343d0aa1995f1d23d5dbe0b03e66ee3f242cd15`; TinyUSB
  `b80f1c107d0a33eb3be055f95fe0b3b9d6c0be48`.
- Pioarduino Core: `ba0ce19fe387aef748db7475e514278d621a6586` (6.2.0).
  Its 6.2.0.post1 version fails the platform's version comparison.
- `dependencies.lock` records resolved IDF component versions and hashes.

The hybrid exporter does not export newly enabled public headers or switch its
imported controller archives/link flags. The script explicitly exports Bluedroid
headers and the three BTDM archives (`libbredr_app.a`, `libble_app.a`,
`libbtdm_common.a`), retains both controller link flags, and
removes the NimBLE `log_printf` wrapper. Generated memory/section scripts and
per-chip `sdkconfig` remain paired with the rebuilt libraries. A second Arduino
link deliberately references PAN, the dual-mode controller, BLE GAP/GATTC and USB MSC to check the
exported package. `freeink-pan.json` records patch/config hashes in the output.
Bluetooth host allocations prefer PSRAM, preserving internal memory for the
controller, display and network buffers. Host task stacks remain internal.

The bounded profile admits one Classic phone connection and one BLE page turner.
BLE buffer counts are limited without reducing packet sizes. USB includes one
CDC port and MSC; unused device classes and the host controller are disabled.
The builder patches TinyUSB's host switch and class counts together, and checks
the exported configuration. Rebuild this package when updating the profile.
The SDK checks internal and DMA-capable heap headroom before the first radio
initialization, because a failed controller startup cannot reliably unwind all
vendor allocations. This admission test is separate from PSRAM capacity.

The certificate-bundle patch allocates callback-owned candidate certificates and
ASN.1 nodes with `mbedtls_calloc`, including matching failure-path frees. IDF's
original callback used libc allocation, but Mbed TLS destroys the returned chain
with its configured allocator. Mixing these allocators corrupts the heap when
the product installs its accounted PSRAM allocator. The exported metadata records
this patch separately; certificate verification and cross-signed trust remain enabled.

Metalio can place CPU-read display line queues in PSRAM with
`EPD_LINE_QUEUE_PSRAM=1`. DMA output buffers remain internal. This option requires
the firmware to link both flash-cache guard wrappers in `FlashAccess.cpp` and
enable `coordinateFlashAccess()` after boot peripherals initialize. The display
worker releases its barrier on completion, so NVS/radio flash writes waiting on
the main task cannot deadlock the refresh. Test the final firmware on hardware;
these settings are not a substitute for simultaneous radio/refresh validation.
`EPD_LCD_FULL_PHASE=1` also sizes those queues for every row in one waveform
phase and publishes only complete phases. This prevents radio scheduling from
starving an active scan; waveform bytes and target phase timing remain unchanged.

This is compilation evidence only. Pairing, BNEP traffic, DHCP, radio teardown,
RTC, simultaneous HID/PAN and iPhone behavior must be validated on hardware separately.

Source-reconstruction regression tests run offline:

```sh
python3 freeink-sdk/tools/bluetooth-pan/test_source.py
python3 freeink-sdk/tools/bluetooth-pan/test_tls_allocator.py --idf .tools/pan-platformio/packages/framework-espidf
```

Set `FREEINK_SOURCE_GH=1` to fetch changed blobs through an already authenticated
GitHub CLI when raw-content downloads are unavailable. The default uses curl;
both transports verify the same pinned blob hashes and cache verified files.
