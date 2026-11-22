# FreeInk desktop backend

This is an opt-in native backend for FreeInk consumers. It is never selected by
PlatformIO or the SDK's ESP-IDF component. The PicoInk consumer builds it through
`simulator/CMakeLists.txt` with `FREEINK_HOST` and the actual Metalio Plus profile.

The native source list replaces display, input, SD and peripheral implementations
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
