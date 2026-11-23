# Testing FreeInk SDK

Run these commands from the repository root. Host tests use production code
with desktop adapters or hardware stubs; they do not require a device or PlatformIO.
You need a C/C++ compiler with C++17 support, Python 3, and a POSIX shell.

## Serial transport

```sh
python3 libs/hardware/BoardConfig/test/host/run_serial.py
```

Checks that native USB Serial/JTAG output is serviced without a blocking flush,
and that UART, TinyUSB, Sticky's UART bridge, and simulator builds remain no-ops.
Device tests must also verify timely acknowledgements and intact transfer data.

## UI, books, credentials, and input

```sh
sh libs/ui/FreeInkUI/test/host/run.sh
sh libs/book/FreeInkBook/test/host/run.sh
sh libs/book/ContentProtection/test/host/run.sh
sh libs/hardware/InputManager/test/host/run.sh
sh libs/hardware/InputManager/test/host/run_metalio.sh
sh libs/hardware/InputManager/test/host/run_async.sh
sh libs/hardware/HapticManager/test/host/run.sh
```

The book suite also needs `zip`. It uses `sips` or ImageMagick's `convert` for
JPEG fixtures and optionally Pillow for progressive JPEG fixtures; the related
checks skip when those converters are unavailable. It builds layout tests for
the default, SMALL, and LARGE memory profiles. Build outputs go under the system
temporary directory.

The layout suite checks incremental plain-text output against one-shot layout,
including early pages, byte progress, I/O failures, abort/restart, and limited
arenas. Both EPUB and TXT also check bounded input chunks against one-shot
output; an idle UI step can yield without waiting for a complete page.
The JPEG symbol test links dummy platform `jd_prepare` / `jd_decomp`
symbols beside the bundled decoder to prevent accidental use of an incompatible
ESP ROM implementation. The consumer ELF and physical board still need checking
because host tests do not use the ESP ROM linker script.

## Progressive JPEG

```sh
python3 libs/assets/ProgressiveJpeg/test/host/run.py
```

Needs Pillow, `cjpeg` and `djpeg`. Decodes generated progressive JPEGs (4:2:0,
4:2:2, 4:4:4, grayscale, restart intervals, odd sizes) and compares the luma
with `djpeg -grayscale`; also checks that baseline files are refused and that
a truncated stream decodes without failing. Set `PJ_EXTRA_JPEGS` to a
colon-separated list of files to check real-world images too.

## Battery gauge

```sh
sh libs/hardware/BatteryMonitor/test/host/run.sh
```

Compiles `BatteryMonitor` and the real X3 board profile against a model BQ27220
that holds the Design Capacity load to TRM SLUUBD4A 6.1: key timing, CONFIG
UPDATE, checksummed Data Memory writes, and the X3's block re-select. Every
refused I2C transaction must still leave the gauge out of CONFIG UPDATE and
sealed, and the next start must finish the load. It also compiles the EEGO A4
profile against a model CW2015: the battery-profile check and upload sequence,
the voltage and percentage conversion, and the ADC fallback when no gauge
answers.

## Motion sensors

```sh
sh libs/hardware/Imu/test/host/run.sh
```

Compiles `Imu` with the Metalio EInk4 profile against a model SC7A20H (and
the QMI8658 and LSM6DS3 scaling), once more with `FREEINK_CAP_IMU=0`, and with
the Paper Mono profile against a model BMI270: `begin()` must only identify
the part, the first `wake()` or `read()` must stream the whole configuration
file through the INIT window in order with a longer bus timeout, a warm part
must skip the upload, and the board's axis flips apply. The model does not run
the configuration file, so the sensor's data path still needs a device.

## Fonts

```sh
sh libs/font/FreeInkFont/test/host/run.sh
sh libs/font/FreeInkFont/test/host/run_ligature.sh
```

Covers `FtFont::RenderOptions` (every `HintingMode` plus monochrome) against a
real bundled font, plus a minimal build with none of the three
`FREEINK_FONT_ENABLE_*` flags set to confirm existing consumers see no change
in output and unsupported requests are reported rather than silently degraded.

Covers `FtFont::ligature()`/`ligatureGlyphId()` (GSUB-based) against a real
bundled font: exact-value assertions for the five standard Latin ligatures,
the codepoint-vs-glyph-ID contract boundary, deinit()/re-init cache
invalidation, and a null-pointer safety check on `Gsub::LigatureGlyphId`.

## BLE HID host

```sh
sh libs/network/BleKeyboardHost/test/host/run.sh
```

Compiles `BleKeyboardHost` with `FREEINK_CAP_BLE_HID_HOST=1` against a fake
NimBLE and FreeRTOS layer, with the connection task on a real thread. Covers
report decode into key events, auto-reconnect, scanning and teardown order.
The fake has no radio, so pairing, link timing and memory use still need a
device.

## Frontlight driver

```sh
sh libs/hardware/FrontlightManager/test/host/run.sh
```

Compiles the Picco frontlight backend and real board profile against a register
model with a wake-settling interval. Covers the first nonzero brightness write,
off/on level restoration, warm/cool channels, slider updates without repeated
wake delays, and retry after a failed wake transaction. Physical light output
and the actual controller settling time still require a device check.

## Display drivers

```sh
python3 libs/display/FreeInkDisplay/test/host/run_bus.py
python3 libs/display/FreeInkDisplay/test/host/run_pro.py
python3 libs/display/FreeInkDisplay/test/host/run_epdiy.py
python3 libs/display/FreeInkDisplay/test/host/run_uc8279.py
python3 libs/display/FreeInkDisplay/test/host/run_uc8279c_a4.py
python3 libs/display/FreeInkDisplay/test/host/run_uc8253_power.py
python3 libs/display/FreeInkDisplay/test/host/run_picco.py
```

These compile production drivers against recording buses. The Pro suite also
compiles the facade in single- and dual-buffer modes, checks every concrete driver
header against the shared interface, and verifies selected controller identity
independently of board metadata. The Picco suite builds the
SSD1677/SSD2677 drivers as a Picco and as a Sticky, including each board's
bit-banged panel probe. The checks cover transfer
sequences, grayscale uploads, refresh lifecycle, and power-state behavior for
the drivers included in each harness.

EPDiy additionally checks the optional board white-conditioning waveform: known
full refreshes use it, ordinary grayscale updates restore the normal waveform,
and a failed refresh still requires the original clear/recovery sequence. Metalio
Plus reuses its existing full-quality 20-phase white erase plus 48-phase GL16
reveal. The waveform suite checks every erase pulse stays neutral or drives
toward white. This does not establish physical residual-image performance.

For the board-profile and display-probe regression test, follow the
[XteinkDetect host instructions](../libs/hardware/XteinkDetect/test/host/README.md).

## Validation limits

Host suites cover only the code and configurations each harness compiles. Run
consumer firmware builds for affected device and capability combinations, then
validate relevant refresh quality, peripheral behavior, and sleep/wake on hardware.
This SDK supplies sample PlatformIO configurations; firmware entry points belong
to the consumer project.

When checking dead code, distinguish private implementation details from public
SDK APIs. A public method, board profile, compatibility wrapper, or capability-gated
driver can have no in-repository callers and still be used by downstream firmware.
Check references across all configurations before removing internal symbols, and
exclude vendored dependencies and generated assets from routine source cleanup.

## SecureNet bulk transfers

```sh
sh libs/network/SecureNet/test/host/run.sh
WOLFSSL_INTERNAL_SOURCE=/path/to/Arduino-wolfSSL/src/src/internal.c \
  python3 libs/network/SecureNet/test/host/test_record_reservation.py
```

The shell suite exercises HTTP control flow with scripted transports, including
reservation timing after the request, cancellation, and resumed transfers. The
Python harness compiles the production reservation/stop methods and the
consumer's pinned wolfSSL buffer functions with a bounded allocator, sanitizers,
and competing clients. It requires the consumer's verified input-retention and
empty-buffer-release patches. It covers both allocation failure before the fix
and retained medium/full records afterward; it does not perform TLS handshakes
or measure device heap. Consumers without `FREEINK_WOLFSSL_RETAIN_INPUT_BUFFER`
use ordinary on-demand record growth without proactive reservation. Reservations
use wolfSSL's own connection-owned buffer and allocator, with no global allocator
replacement or cross-client loan.

## Streaming JSON previews

```sh
sh libs/network/JsonSax/test/host/run.sh
```

Checks optional bounded string prefixes, unchanged default overflow handling,
continued parsing after long strings, and rejection of oversized keys under ASan/UBSan.

## Metalio Plus integration regressions

```sh
sh libs/hardware/BoardConfig/test/host/run_metalio_plus.sh
sh libs/hardware/InputManager/test/host/run_async.sh
sh libs/hardware/AudioManager/test/host/run_metalio.sh
python3 libs/hardware/Rtc/test/host/run.py
sh libs/hardware/SDCardManager/test/host/run_native.sh
sh libs/hardware/UsbMassStorage/test/host/run.sh
```

The async input and USB tests run production worker/callback code on real host
threads. The native SDMMC model deliberately hides a write error after a failed
transaction, checking that the adapter still reports failure. BatteryMonitor's
standard suite also includes CX25601N setup, quantization, status, recharge and
initialization fault injection. All of these complement, rather than replace,
the consumer build and physical board checks.

## Application PSRAM and waveform storage

The host resource suite (`host_resources` in PicoInk's stress simulator build)
checks `ApplicationMemory.h`: construction and destruction of external arrays,
zero initialization, preservation on failure and no internal fallback when
PSRAM is exhausted. The total-capacity query must remain stable as free space
changes, so font allocation cannot mistake exhaustion for a no-PSRAM board.

```sh
sh libs/hardware/BoardMetalioEInk4Plus/test/host/run.sh
```

The waveform test compares all 15 mode/profile digests to the pre-migration
`23701d2` baseline and checks external allocation capabilities. It does not
validate panel timing or optical output. For hosts where the ASan runtime cannot
start, the font runner accepts an explicit `FREEINK_TEST_SANITIZERS` override;
record this limitation separately from passing UBSan tests.

## Timestamped key edges

`InputManager::popKeyEvent` returns debounced press/release edges with their
capture timestamps. Queue overflow emits button `255` and discards ambiguous
edges so a consumer can cancel its gesture. Existing press/HOME queues remain
available to other FreeInk consumers. The desktop adapter follows the same edge
contract and now makes `FsFile` move-only, matching SdFat's production ownership.
Run `libs/hardware/InputManager/test/host/run_async.sh` as well as the input suites
above; the desktop `host_input` test covers timestamps, release and overflow.

## Bluetooth PAN

```sh
python3 libs/network/BluetoothPan/test/host/run.py
python3 libs/network/NetworkTimeSync/test/host/run.py
```

Checks capability gating independently of radio initialization: Classic hardware,
feature opt-in, Bluedroid and PAN are all required. BLE-only, disabled and host
builds return false. The real PAN worker/callbacks also run against deterministic
radio/RTOS fakes: start/stop races, SSP confirmation/rejection, IP acquisition,
link retry, forgetting, late connections, bounded Ethernet RX and congestion.
Twenty repeated enables verify runtime resource cleanup. NTP tests cover packet
validation, 2036 era rollover, DNS failure, timeouts, cancellation and stale results.
Both lifecycle suites enable undefined-behavior checks. PAN additionally accepts
`SANITIZERS=address,undefined` when the host compiler/runtime supports it.
Hardware pairing, DHCP, power and network access require a real device.

The desktop `host_volumes` target also exercises the shared VolumeStorage
facade: separate TF/internal contents, moves and handle accounting, exclusive
reservation, remount without formatting, path validation and concurrent callers.
The host internal directory models file ownership, not FAT/WL flash behavior.

The USB MSC suite also compiles `FlashUsbStorage` against a deterministic
wear-levelling backend. It covers native 4096-byte FAT sectors, partition/bounds
validation, erase failures and remount persistence. These host checks do not
replace real USB host enumeration, flash wear-levelling or cable-removal tests.
