# S31 PSRAM copy diagnostic

`psram_copy.cpp` is a standalone Arduino test using the official asynchronous
memcpy driver. It never mounts storage. Build it with the same S31 board,
framework, CPU and PSRAM settings as the application. Deploy only its application
image to the existing application partition and restore the production firmware
after collecting `COPY_DONE`; do not initialize a storage partition.

It alternates CPU/DMA order across ten repetitions at six sizes. DMA timing
includes descriptor setup, driver cache synchronization and completion. Every
copy is compared byte for byte. Buffers remain allocated if completion times out.

On the 320 MHz / 16 MiB / 250 MHz PSRAM Metalio, 2026-11-23:

| Bytes | CPU median µs | DMA median µs |
| ---: | ---: | ---: |
| 64 | 2 | 120 |
| 512 | 4 | 124.5 |
| 4,096 | 22 | 189 |
| 65,536 | 1,039 | 1,128.5 |
| 262,144 | 4,506 | 4,157 |
| 831,744 | 14,291 | 12,947 |

All 120 copies matched. This isolated result saves about 1.3 ms for a full page;
it does not establish an end-to-end Reader improvement or coexistence with panel
scanning. Production copying therefore remains on the CPU.
