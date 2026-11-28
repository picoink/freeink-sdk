# FreeInk desktop backend

This is an opt-in native backend for FreeInk consumers. It is never selected by
PlatformIO or the SDK's ESP-IDF component. The PicoInk consumer builds it through
`simulator/CMakeLists.txt` with `FREEINK_HOST` and the actual Metalio Plus profile.

The native source list replaces display, input, storage and peripheral implementations
while compiling the production book, font and UI engines. Public peripheral
headers and BoardConfig are reused; the display, input and SD compatibility
headers expose the subset exercised by this consumer. These are application
models, not substitutes for the SDK's electrical/driver tests.

UI observation is enabled only with `FREEINK_HOST`. Text and interaction tables
are copied on the firmware thread and attached to the submitted display frame.
The frontend never reads mutable application objects. Serial output goes to
stderr, leaving stdout available for the control protocol.

## Upstream provenance

CrossPoint Simulator: https://github.com/crosspoint-reader/crosspoint-simulator
at `212e764e5cb608f050ee595a5a66dab476a2ae11`, MIT.

- `include/WString.h`: selected Arduino String compatibility.
- `src/Storage.cpp`: adapted POSIX file/directory ownership from `HalStorage.cpp`.
- The consumer SDL frontend follows `HalDisplay.cpp` and `HalGPIO.cpp` for
  main-thread presentation and physical-key mappings. It uses this backend's
  FreeInk interfaces instead of importing the CrossPoint HAL.

The upstream license is retained in `third_party/crosspoint-LICENSE`.
`third_party/picojson.h` is picojson v1.3.0 (kazuho/picojson); its license is
embedded in the header. It is used for host control/state only.

## Tests

Build the consumer, then run `ctest --test-dir simulator/.build/fast
--output-on-failure`. The host backend test checks filesystem ownership,
path containment, persisted preferences, input queue edges and the production
UI transform in all four rotations. Existing library host suites remain required
when changing those libraries. Hardware validation is recorded separately.

## Storage volumes

Call `configureStorage(root)` before firmware setup. It creates `nvs/`, `flash/`,
`tf/` and `.state/` beneath that root, records their canonical paths in
`runtime().storage`, and invalidates previous volume mounts. `storageInfo()`
returns those paths for frontend diagnostics. There are no legacy path fallbacks.

`SdMan` mounts `tf/`; `LittleFS.begin()` mounts `flash/`. File handles carry their
own volume and mount generation, so TF detach/remount or shutdown does not affect
Flash, and stale handles never become valid again on remount. Both volumes reject
parent traversal and symlink escapes. The LittleFS subset includes `begin/end`,
`open` with r/w/a and their + modes, `exists`, `mkdir/rmdir`, `remove/rename`, and
basic file read/write, seek, flush, names and directory iteration. It models files,
not a partition image; mount labels/max-open-files are not modeled, and
`formatOnFail` never deletes host data. There is no raw formatting/erase API.
The existing `sd*` fault and pressure settings apply only to TF. Flash supports
`flash`, `flash_mount`, `flash_read`, `flash_write` and `flash_sync` faults without
inheriting TF bandwidth/latency settings.

`Preferences` uses `nvs/<namespace>.json`, containing a plain JSON object. It
supports the consumer's unsigned 8/16-bit values and booleans, resets handle state
on `begin/end`, and merges current on-disk keys before atomically replacing the
file. Read-only handles cannot write. Missing writable namespaces use defaults;
malformed JSON and non-regular files report errors and remain untouched. Changes
made manually while stopped are loaded on the next `begin`/process start.

`host_storage` verifies these behaviors in temporary directories, including
independent Flash/TF ownership, namespace isolation, corrupt JSON preservation
and file path boundaries. Storage test failures exit nonzero without aborting.

## Optional resource model

The consumer's `PICO_STRESS` build selects `MemoryStress.cpp`; normal builds use
`Memory.cpp` and do not intercept C/C++ allocation or instrument CPU work.
Resource settings are supplied before firmware setup. Internal and PSRAM arenas
have separate capacities, aligned blocks, coalescing free holes, failure
injection and peak/largest-free-block statistics. Thread-local `HostScope`
excludes native presentation, filesystem bookkeeping and protocol allocations.
On macOS executable allocation symbols use a raw malloc-zone path; Linux uses
linker wrapping. Compiler coverage checkpoints slow actual application/book/font
work using thread CPU time. This is a pressure model, not instruction emulation.

`host_resources` exercises both C and C++ allocations, capability constraints,
fragmentation/coalescing, realloc failure preservation, one-shot failures and
actual instrumented compute. Sanitized builds poison arena holes and block guards.
Static objects and omitted driver allocations need an explicit reserved budget;
MCU stack size and ABI differences are not reproduced by the native process.

## Desktop input semantics

Set `FREEINK_HOST_CHECK_FLASH_BARRIER=1` in integration tests to reject internal
flash file access that would wait behind the S31 display barrier. The host
reports an exception instead of blocking forever. Admitted foreground/content
work can still drain through nested flash reads. This models admission ordering,
not physical flash timing; consumer cold-font boot and hardware tests remain
required.

The host backend models all seven Metalio Plus inputs separately: the three
cover keys (HOME/PREV/NEXT), BOOT, volume up/down and power. Device levels and
source ownership are merged before producing the firmware button edges. HOME
emits a short event on release or one long event at 700 ms while held; cancelling
a source does not manufacture a short HOME event. Physical power levels remain
readable while firmware waits for release during sleep preparation. The SDL
thread can safely update input while the firmware thread polls the queues.

`host_input` checks these semantics independently of SDL, including overlapping
sources and release after polling stops. Consumer SDL tests additionally verify
the real event-filter coordinate path, keyboard aliases and focus cancellation.
