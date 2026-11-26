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

The page compositor's Gray8 tests compare all four rotations with an independent
scalar renderer, using every coverage level, negative bearings, clipping,
padded rows, overlapping glyphs, synthetic bold and underline. The optimized
glyph path writes contiguous physical rows, including portrait framebuffers.
Device response-time measurements belong to the consumer's hardware report.

## Progressive JPEG

```sh
python3 libs/assets/ProgressiveJpeg/test/host/run.py
```

Needs Pillow, `cjpeg` and `djpeg`. Decodes generated progressive JPEGs (4:2:0,
4:2:2, 4:4:4, grayscale, restart intervals, odd sizes) and compares the luma
with `djpeg -grayscale`; also checks that baseline files are refused and that
a truncated stream decodes without failing. Set `PJ_EXTRA_JPEGS` to a
colon-separated list of files to check real-world images too.

## Persistent thumbnail JPEGs

`ImageAccelerator::encodeGray` accepts bounded, MCU-aligned grayscale images.
The S31 implementation serializes decoder/encoder transactions on the content worker
and uses the official DMA allocation, synchronization and completion APIs. The
desktop adapter uses libjpeg and never advertises hardware capability.

The consumer `content_tasks` test checks aligned JPEG output, tiny-output failure
and loading a cached image while its original book is unavailable. Its
`reader_cover_cache` integration test checks persistent generation, warm disk
hits without rewriting files, Home memory reuse and corrupt-cache regeneration.
Run these and `image_quality`, the SDK host suites and the Metalio build before
committing. Device acceptance must observe actual cache hits with JPEG hardware
execution; successful host encoding is not hardware validation.
The PicoInk S31 device run generated and validated six 160 x 240 Q90 cache
JPEGs. Rebooted disk hits each executed the hardware decoder in 0.97–1.10 ms
(21–24 ms complete cache tasks). The codec retains its early-created handles;
the encoder's CPU-written header must not be invalidated with the DMA payload.

The FreeInkUI Gray8 suite also compares runtime-glyph rendering with independent
scalar golden frames in all four orientations, including every alpha value,
negative bearings, clipping, padded rows, colors and overlapping text. Runtime
glyphs now traverse physical rows after clipping once per glyph. Device timing
is recorded separately by the PicoInk consumer; pixel equivalence alone does
not establish a hardware speedup.
Rounded-border straight spans also use batched fills; the corner and inner-radius
predicates remain unchanged. The original scalar scene goldens cover the new
path with clipping, corner masks, narrow rectangles and all four orientations.

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
Also compares width-only advances against full glyph metrics across sizes,
hinting modes, Latin/CJK characters and synthetic styles. Set
`FREEINK_FONT_ADVANCE_FIXTURE` to an additional device font to test its metrics.

Covers `FtFont::ligature()`/`ligatureGlyphId()` (GSUB-based) against a real
bundled font: exact-value assertions for the five standard Latin ligatures,
the codepoint-vs-glyph-ID contract boundary, deinit()/re-init cache
invalidation, and a null-pointer safety check on `Gsub::LigatureGlyphId`.

## BLE HID host

```sh
sh libs/network/BleKeyboardHost/test/host/run.sh
sh libs/network/BleKeyboardHost/test/bluedroid/run.sh
```

Compiles `BleKeyboardHost` with `FREEINK_CAP_BLE_HID_HOST=1` against a fake
NimBLE and FreeRTOS layer, with the connection task on a real thread. Covers
report decode into key events, auto-reconnect, scanning and teardown order.
The fake has no radio, so pairing, link timing and memory use still need a
device.

The Bluedroid suite drives asynchronous GATT discovery, security, report
subscription and input. It covers scan/open teardown races, timeout cancellation,
notification flooding and retrying the final host release while retaining PAN.
The PAN suite also checks both lease-release orders and shared-host failures.
Neither suite establishes a radio connection. The Bluedroid runner also drives
the PageTurner worker with deterministic task stepping: per-device bindings,
release-based learning, save rejection, full-queue cancellation, input and sleep
generation acknowledgements.

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

EPDiy calibration tests compare in-place packed Gray4 translation with an
independent scalar image for positive/negative odd/even pixel offsets, clipping,
white exposed strips and allocation canaries, including the 1216 x 684 panel.
They check inverse touch mapping, invalid/busy refusal, immutable source pixels,
clean refresh after changing calibration and subsequent GL4 operation. Calibration
uses the existing PSRAM scan target, with no scratch frame or SRAM row buffer.
The host backend applies the same physical offset to screenshots and UI hit
regions. Consumer tests must cover persistence failures and physical edge
visibility; assembly alignment cannot be diagnosed from a framebuffer alone.

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

USB tests also compile the Metalio switch routing: failed starts and completed
teardown restore the debug PHY, while in-flight storage callbacks keep ownership
until drained. Re-entry ignores a stale TinyUSB configured flag until a fresh
mount event or actual I/O. The SDMMC suite checks that an active card retains its
DMA buffer when allocation is unavailable. Filesystem remount after MSC reuses
that active card, with one cold retry if mounting fails.

Physical PicoInk validation alternated internal FAT and TF exports twice without
resetting. All four volumes mounted on the host, safely ejected, restored the
debug connection and returned to Reader with SD available. The retained SD DMA
buffer allowed remount even with a largest free DMA block of only 2,548 bytes.
These observations are separate from host tests and the Metalio firmware build.

The PCF8563 timer model also reproduces the Metalio's nonzero unused-bit reads:
Control_status_2 bit 6 and Timer_control bits 6, 4 and 2 read high. Timer setup
must verify only TE/TD, and writes must not copy unused bits. See the
[PCF8563 register definitions](https://cache.nxp.com/docs/en/data-sheet/PCF8563.pdf),
sections 8.3.2 and 8.8.1. The pre-fix implementation fails this regression.
The RTC runner accepts `FREEINK_TEST_SANITIZERS=undefined` on hosts whose ASan
runtime cannot initialize; record that explicitly rather than claiming an ASan
pass. The PicoInk validation host used this override, as with its font tests.
On the physical S31, the corrected 60-second timer produced `RTC timer wake
detected` after 61.97 seconds; Reader, SD and cached covers recovered afterward.

Metalio Plus boot rail initialization uses the existing three-second retry
window before consumers mount TF. Previously a transient expander NACK at boot
activated the nonblocking retry gate, so the immediately following TF mount was
skipped even though later display initialization recovered the expander. The
board host test now models delayed expander readiness and a persistent failure;
it checks both successful rail setup before returning and the bounded timeout.

## Application PSRAM and waveform storage

The host resource suite (`host_resources` in PicoInk's stress simulator build)
checks `ApplicationMemory.h`: construction and destruction of external arrays,
zero initialization, preservation on failure and no internal fallback when
PSRAM is exhausted. The total-capacity query must remain stable as free space
changes, so font allocation cannot mistake exhaustion for a no-PSRAM board.

The categorized `host_resource_governance` suite replaces the shared optional
cache budget. It tests category admission, physical reserve, fragmented heaps,
delta realloc, owner generations, cross-owner release rejection and reclamation.
Consumers must use matching managed release calls and report unclassified heap
usage. Physical free-space admission includes unmigrated driver/library blocks.

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

`sh libs/hardware/SDCardManager/test/host/run_internal.sh` exercises the device's
POSIX internal-file helper against real temporary files: missing files, exclusive
creation, append after seek, read-only protection, truncation, directory iteration
and repeated opens. Device internal storage uses descriptors directly and `stat`
for existence checks; it no longer creates temporary Arduino stdio streams and
their libc mutexes before reopening the same file. This test does not emulate
ESP heap pressure or FAT/WL. PicoInk records cold-open and response-time hardware
validation separately.

PSRAM consumers can call `memory::preferExternalHeap()` after initializing
boot-critical peripherals. It routes ordinary task-side library allocations to
PSRAM first; explicit internal/DMA allocations and FreeRTOS stacks/locks keep
their capability requirements. This avoids the Arduino 4 KiB default-allocation
threshold exhausting SRAM during concurrent display and content work. The host
backend does not change its process allocator; actual SRAM headroom and display
stability must be measured on the device.

The USB MSC suite also compiles `FlashUsbStorage` against a deterministic
wear-levelling backend. It covers native 4096-byte FAT sectors, partition/bounds
validation, erase failures and remount persistence. These host checks do not
replace real USB host enumeration, flash wear-levelling or cable-removal tests.

## Packed 16-level bitmaps

`BitmapFormat::Gray4` stores two pixels per byte, high nibble first, with 0
black and 15 white. The Gray8 target expands each level to `level * 17`.
The UI host suite checks all display/image rotations, odd widths, clipped and
scaled rectangles, padded output rows, and every gray level against an
independent scalar frame. This verifies pixel values, not optical panel levels.

## S31 LCD pipeline and failure containment

```sh
python3 libs/display/epdiy/test/host/run.py
python3 libs/display/FreeInkDisplay/test/host/run_epdiy.py
```

The pipeline suite compiles the production LUT, row queues, difference conversion
and overlapped phase scheduler with a threaded fake LCD. It compares each emitted
row with independent scalar waveform output over 684 rows, with either CPU owning
the IRQ. Incomplete scans and missing completion interrupts must drain both feeders
before queue/LUT reuse; subsequent cycles must recover without stale phases. UBSan
also checks unaligned packed difference input, every width from 1 to 1216, and odd
tails. The driver suite injects a failed scan and verifies that automatic retries
cannot clear or scan the panel again until an explicit resync.

The row-layout test checks the first and last payload byte of all 684 rows against
an independent receiver, for both native S31 RGB and legacy dummy-phase layouts.
S31 uses 304 active bytes for a 1216-pixel panel, with no leading dummy byte.
The DMA row stride, active width and enabled peripheral phases must agree.
The fake LCD cannot verify physical start-pulse timing or glass alignment: device
validation must also inspect all four edges using a known border pattern, after
clearing any marks left at the old scan position. A correct framebuffer digest
and successful scan completion alone do not establish physical alignment.

The S31 full-phase scheduler balances row preparation across both CPUs. One
complete phase fits in the row queues; two immutable PSRAM wire images exchange
preparation/scan ownership. The next image is packed while the prior image scans,
without altering the active descriptor chain or completion cursor. This replaces
the second queued phase within the existing System budget. Direct DMA removes
the eight-row refill interrupt deadline; a latched hardware EOF still has to
confirm payload consumption at physical panel completion. The host fake verifies
overlapping packing, exact phase bytes, small queue capacity and cancellation of
an already prepared next image. Streaming S31 retains its measured 13:19 feeder
split. Cancellation waits for old callbacks before recycling data. Other LCD
targets retain alternating ownership. EPDiy exposes separate power,
preparation, waveform-cycle, actual scanning and completion timings through
`RefreshMetrics`; the API contains no ESP-IDF types.

FreeInkUI caps automatic retries after two failed presentations. A temporarily
busy surface uses `deferPresentation()` and does not consume that failure budget.
Input regions remain unpublished until presentation succeeds. Real device timing,
underruns, modal appearance and optical gray levels must be validated separately;
the PicoInk consumer records those results in its responsiveness validation log.
After a successful explicit resync, `recoverPresentation()` requests a fresh app
frame before reopening input. The PicoInk surface test distinguishes a new
successful presentation timestamp from stale success or another failed attempt,
including an intervening status read.

The LCD clear row is reserved at renderer initialization and retained until
deinitialization (304 bytes on Metalio Plus). Streaming LCD targets use internal
DMA SRAM. The S31 full-phase path invokes its callback only while packing on a
task, so its clear row, 4 KiB waveform table and 304-byte mask now use managed
PSRAM. These 4704 bytes no longer compete with radio SRAM, without changing the
System quota. DMA descriptors and interrupt completion metadata stay internal.
On S31, ordinary `malloc` and `MALLOC_CAP_INTERNAL` may fall back to LP RAM;
an ISR reading the clear row there missed the bounce-buffer refill interval.
The pipeline suite now checks cropped black, white and neutral clear pixels,
buffer ownership across repeated clears, interrupted scans and lost completion.
Clear failures also reach refresh fault metrics. UBSan findings stop the suite.
Physical validation is separate: the PicoInk consumer records the LP address,
refill timings, successful flashing and repeated-boot checks.

## Wake classification

`PowerManager::wokeFromSleep()` identifies a completed deep-sleep boot using the
reset reason and wake source. Cold boot and software reset do not count. The
SDK host backend models this through `wake_from_sleep`; `host_backend` tests
both states. PicoInk's process-restart integration tests verify consumption of
one-shot recovery records. These checks and the Metalio firmware build passed
on 2026-11-24; physical wake classification has not been validated for this change.

## Categorized resource admission

`host_resource_governance` in the stress simulator exercises `ResourceMemory.h`:
independent category ceilings, two-MiB physical reserve, generation-bearing
owners, rejected foreign release, incremental realloc/shrink, failure rollback,
and accounting cleanup. It also occupies unclassified PSRAM directly and checks
that managed allocations preserve the reserve without consuming internal RAM.
The header is opt-in; consumers must migrate paired allocation/release calls and
report unclassified heap usage until their allocation paths are covered.

## Page-pinned glyph storage

The host `host_glyph_pages` test exercises `PageGlyphArena` with two blocks:
current and candidate page pins, shared glyph references across promotion,
refusal when all storage is protected, stale-generation rejection, empty glyphs
and oversized glyph bypass. The arena has fixed metadata and borrows its pixel
storage from the caller; it does not allocate or scan the heap. Reclamation
checks at most 64 block descriptors and never overwrites a protected block.
PicoInk's `document_api` test also floods a 16 KiB font cache and compares the
held displayed glyph bytes, then checks cached and uncached raster output over
repeated wraps and font sizes. Target page-turn latency remains a device check.

Resource governance checks distinguish a local category refusal from physical
exhaustion: the former may evict only that category, while reserve/fragmentation
failures request global reclamation. The consumer content test exercises reverse
page prefetch and a transient-image quota refusal with no room for a third frame.
Device acceptance must include sustained turns in both directions; alternating
between two pages does not exercise this working set.


## S31 compact GL4 and ready-driven power

`python3 libs/hardware/BoardMetalioEInk4Plus/test/host/run_power.py` checks the
TPS65185 I2C wake minimum, temporary NACKs, elapsed power-good deadlines and
32-bit clock wrap. The production path waits at least 3 ms before I2C (the
[TI SLVSAQ8G specification](https://www.ti.com/lit/ds/symlink/tps65185.pdf),
Figure 2, requires 1.8 ms), then checks both the power-good pin and rail status.
It preserves voltage, sequence registers, retry power cycles, output-enable
ordering and refresh-after power-off. The test models readiness and failures;
it is not an electrical or temperature qualification of the panel.

The LCD suite exhaustively compares the compact 4 KiB pair LUT with scalar
waveform pixels, including four target gray values, all sixteen old values,
unchanged-pixel suppression, tail words and fused difference construction.
It does not reduce waveform stages. Gray8 packing combines quantization and
original-target preservation; driver tests verify the source remains immutable.

Physical observations are tracked by the PicoInk consumer. A 2026-11-25
30-turn test on S31 completed without scan faults after the readiness change,
and the user confirmed normal text and screen appearance. This is a single
board result, not evidence for all temperatures or supply conditions.

`sh libs/hardware/BoardConfig/test/host/run_metalio_plus.sh` also checks early
FT6336U reset preparation. `InputManager::prepareBoot()` starts the existing
10 ms low pulse before storage initialization; `begin()` waits out any remaining
300 ms high interval before probing. Repeated preparation does not restart the
clock, and calling `begin()` alone retains the original full wait. No touch
settling interval is shortened. Input and ordered-queue host suites and the
Metalio build pass; physical wake timing is recorded by the consumer.

Wake handoffs can arm a 16-byte retained receipt after durable publication.
`PowerManager::consumeWakeReceipt()` distinguishes a fresh, consumed or unavailable
receipt. Consumers must check the wake reason, consume before application code,
and use durable consumption when RTC retention is unavailable. Host state carries
receipts through simulator process resets; backend tests cover mismatches,
repeat consumption and re-arming identical checkpoints. This removes no durable
progress write and does not treat an ordinary reset as a sleep wake.

`TEXT_CLEAN_REFRESH` retains the complete unknown-panel clear sequence, then draws
fast text with the selected 24-phase reading waveform. Known-panel cleaning keeps
the board's existing white-conditioning waveform. UI and high-quality text keep
16-gray targets. The EPDiy worker test checks initial clearing, waveform selection,
source immutability and subsequent cleaning. Physical startup timing and optical
checks remain consumer evidence, separate from these host checks.

## Read-only prefetch during LCD scanning

`host_resource_coordinator` checks separate read-only content admission: display
may overlap that client, while USB and sleep wait for it to drain. Flash access
cannot inherit a write lease from read-only work. `host_volumes` checks the scoped
storage restriction against existing and newly opened handles: TF reads work,
internal-volume reads, writes, truncation, rename/remove, directory creation and
mount changes are rejected. The scope is eligible only on the S31 LCD/SDMMC
backend (and its host model); it is not a promise of safe shared-SPI concurrency.
PicoInk's `content_tasks` test completes a TF chapter, prepares a neighboring
frame while the display barrier remains asserted, then transfers the frame under
the two-frame budget. Actual DMA coexistence and optical quality require hardware
validation; these host tests cannot certify it.

The EPDiy LCD pipeline host test also replays exactly equal consecutive waveform
phases from the existing immutable full-phase queues. It verifies every output
pixel, queue wraparound, both IRQ core assignments, a failure during replay,
missing completion and recovery. Streaming queues retain normal preparation.
No drive phase, voltage, scan timing or clear stage is removed. The E0470 GL4
profile has 14 repeated phases out of 24; no additional framebuffer is allocated.

Consumer hardware trials `1cdf5ed9eb7351cc` and `ba97186e6f706a8e` completed
distinct-page and alternating turns with zero scan faults; feed work dropped
from approximately 269/280 ms to 112/114 ms per normal GL4 update. Those are
measured producer timings, not a claim of the same reduction in panel latency.
The complete lifecycle/concurrency acceptance remains in PicoInk's validation
report, including its separately recorded optical confirmation.

`host_resource_governance` also tests lazy recovery of reclaimed shared font-file
blocks. Recovery may select a smaller cache than originally requested, leaves
64 KiB of font workspace and respects global/physical reserve admission. A full
quota reads directly without repeated allocation failures; after pressure is
released, subsequent reads become cache hits again. `font_workers` additionally
reclaims the cache from inside a miss callback to check lock and generation safety.

## RISC-V task-local layout

`python3 tools/firmware/test_tls_layout.py` verifies the ELF layout gate.
Metalio consumers install `post:freeink-sdk/tools/firmware/platformio_tls_gate.py`
as a PlatformIO extra script and run `check_tls_layout.py firmware.elf` before
flashing retained binaries. The gate compares PT_TLS against the exact linker
symbols used by FreeRTOS to copy initialized bytes and zero task-local state.

GCC can emit every zero-initialized task local into `.tbss`. An empty `.tdata`
then becomes ordinary alignment padding: the FreeRTOS copy base may precede
PT_TLS by four bytes. A layout-only code change reproduced a nonzero read-only
storage flag at boot. ResourceMemory places its existing zero-initialized Owner
slot in a real aligned TLS data section, with no extra per-task bytes. Build
validation remains necessary if a consumer changes linker scripts or introduces
larger-alignment task locals. This is separate from TLS network encryption.
