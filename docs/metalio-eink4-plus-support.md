# Metalio E-Ink4-Plus support

Build flag: `-DFREEINK_DEVICE_METALIO_EINK4_PLUS=1`. Profile: `METALIO_EINK4_PLUS`
in `BoardConfig.h`. Pin roles and power sequences come from the vendor firmware
([CloudZao/Metalio-E-INK4-Plus](https://github.com/CloudZao/Metalio-E-INK4-Plus),
board directory `main/boards/metalio-e-ink4-plus`, hardware rev 1.3/1.4). None of
the SDK support has been validated on hardware yet.

## Toolchain

The board is an ESP32-S31 (RISC-V), which needs Arduino-ESP32 4 on ESP-IDF 6.1.
Build with the pioarduino `61.04.00-RC1` platform (board `esp32-s31-coreboard-1`),
which in turn needs PlatformIO Core 6.2 or newer. See the `metalio_eink4_plus` env
in `platformio.sample.ini`. The S31 is its own MCU family (`FREEINK_MCU_S31`) and
does not share a binary with other boards.

## Hardware

| Block | Wiring |
|---|---|
| Panel | E0470A14-AF, 4.7" 1216x684 raw parallel, 8-bit bus D0-D7 = GPIO8-15, XCL 16, XLE 17, XOE 18, XSTL 4, MODE 19, SPV 35, BORDER 37, CKV 40 |
| Panel power | TPS65185 at 0x68; WAKEUP/PWRUP/VCOM_CTRL on TCA9555 P0.2/P0.1/P0.0, PWR_GOOD on P0.7; VCOM 1200 mV |
| I2C | SDA 0, SCL 1: TCA9555 0x20 (INT GPIO2), FT6336U 0x38, PCF8563 0x51, BQ27220 0x55, CX25601N 0x6B, SC7A20H 0x19 |
| Touch | FT6336U, INT GPIO5, reset TCA9555 P1.2 |
| Keys | BOOT GPIO61 (confirm); volume up/down P1.0/P1.1 and power P1.7 on the TCA9555, active low; cover keys on the touch panel |
| SD | 4-bit SDMMC: CLK 24, CMD 25, D0-D3 20-23 |
| Frontlight | cool GPIO6, warm GPIO7 (YX6016N EN/PWM, 20 kHz, 10-bit) |
| Power off | pulse P1.3 (100 ms high, 100 ms low) until the power switch cuts power |

## Display

The panel is driven by `EpdiyDriver` over the EPDiy submodule at
`libs/display/epdiy` (LGPL-3.0; [Free-Ink/epdiy](https://github.com/Free-Ink/epdiy),
a fork of [vroland/epdiy](https://github.com/vroland/epdiy) 2.1.3 with ESP32-S31
support taken from the vendor port). Run `git submodule update --init
libs/display/epdiy` before building. The board library `BoardMetalioEInk4Plus`
supplies the EPDiy board definition (TPS65185 sequencing through the expander,
LCD bus pins and line timing) and the panel waveforms.

The host renders in the native 1216x684 frame. Full and Half refreshes use GC16,
Fast uses DU. Grayscale uses the overlay masks onto the displayed B/W page (dark
and light gray) with GL16, or GC16 after a Full or Half base. The panel rails are
powered only for each waveform.

## Input

The FT6336U reports the portrait 684x1216 frame. The profile maps it to the
native panel with `swapXY` and `flipY` (the vendor's `x = rawY, y = 683 - rawX`).
The cover-glass keys report as touch points at rawY about 1600: HOME at x 100,
PREV at x 200 (BTN_UP) and NEXT at x 300 (BTN_DOWN), each within 30 counts.

## Audio, sensors and power

The Plus profile enables `AudioManager` and `Microphone` using UART1
RX38/TX39 and slave I2S BCLK42/WS43/DOUT44/DIN45. The module must acknowledge
`SET MODE 1` before either stream is exposed. Both owners share one duplex bus;
releasing the last owner powers off the module rail. Capture and playback use
16 kHz PCM; failed initialization unwinds the allocated channels and rail.

`Cx25601n` in BoardConfig is a separate charger backend, based on vendor commit
`7cfae83fba5fbaf6c4f4388553c0406e989509e0`. It verifies split-register writes,
keeps charging disabled on programming failure, uses the rev1.4 4.4 V battery
policy, and quantizes nominal 500/1000/1500 mA limits to 480/960/1440 mA. The
consumer periodically calls `service()` with fresh gauge validity flags; a low
battery result requests orderly application shutdown before BATFET cutoff.
`BatteryMonitor` reports charger state and external power independently of the
BQ27220 percentage. Do not configure this chip as a BQ25896.

`Rtc` supports PCF8563 countdown interrupts, with second resolution through
255 seconds and rounded-up minutes through 255 minutes. Alarm flags survive
timer cleanup. Invalid dates, stopped clocks, I2C errors and readback failures
are reported. `Imu` provides optional SC7A20H AOI1 motion interrupts and
`silenceInterrupt()` for releasing the shared GPIO2 line. Orientation/gesture
policy remains the consumer's choice.

`FrontlightManager` drives the warm/cool PWM pair, with 100% user brightness
limited to 10% electrical duty. `ready()` reports PWM attachment failure.
`HapticManager` drives the existing expander motor backend.

Input now queues cover HOME taps/holds with `popHomeKey()`. `endAsync()` waits
for the worker to finish its current poll; a timeout cancels the stop request.
Before sleep, stop input, audio/microphone and haptics, silence the IMU, flush
and close storage, park the frontlight and finish display shutdown.
`PowerManager::armPowerButtonWakeup(true)` retains the RTC interrupt as an
expander input as well as the power key. Check the return value: on failure,
restore the expander with `restoreAfterSleepAbort()` and restart the peripherals.

## SD and USB ownership

Use `-DUSE_BLOCK_DEVICE_INTERFACE=1 -DFREEINK_CAP_USB_MSC=1` and the
`UsbMassStorage` library for whole-card export. GPIO36 selects the USB device
route; S31 uses its native UTMI peripheral, independently of USB Serial/JTAG.

Close **all** file users before `detachFilesystemForRawAccess()`. This now
checks FAT/exFAT cache synchronization before releasing the filesystem. The
consumer must suspend background reader/cache/cover/font jobs throughout MSC.
`end()` rejects new callbacks and waits for in-flight I/O before releasing the
block device. Eject synchronizes the card; errors remain visible. Remount with
`SDCardManager::begin()` after ending MSC, then rescan application data.

The SDMMC adapter uses a bounded aligned internal DMA buffer, checks sector
ranges, and preserves S31 write failures that IDF's multi-block recovery might
otherwise hide. S31 shutdown releases the dedicated SDIO pads to GPIO and
holds them low. `shutdown()` returns false on a filesystem sync failure.

## Validation

The PicoInk PlatformIO consumer builds with Arduino 4.0.0-RC1 and pioarduino
Core 6.2.0. Its S31 target uses QIO/80 MHz because that Arduino package supplies
only `bootloader_qio_80m.elf`; other targets retain their existing flash mode.

Host suites cover audio/microphone lifetimes and ACK failures, CX25601N
quantization and 79 initialization fault points, RTC timers and transaction
faults, IMU register/scaling behavior, expander sleep rollback, async HOME and
shutdown/restart, SDMMC failed initialization and swallowed write errors, and
USB unaligned I/O, eject/re-entry and concurrent callback drain. Commands are
listed in `docs/testing.md`.

Hardware validation is still required for panel waveform/VCOM, touch cover-key
thresholds, PWM light output, sound/microphone levels, charge current/thermal
behavior, SD/USB file integrity and battery/RTC wake with measured sleep current.
Camera, 4G and additional networking are outside this reader integration.
