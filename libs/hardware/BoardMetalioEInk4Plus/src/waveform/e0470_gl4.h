// SPDX-License-Identifier: Apache-2.0
// Derived from PicoInk's E0470 GL4 profile. Preserve the vendor source tables,
// voltages and scan cadence; reject a source other than the calibrated GL48.
#pragma once
#include <epd_internals.h>
#include <string.h>
#define E0470_GL4_SOURCE_PHASES 48
#define E0470_GL4_PHASES 24
static inline int e0470_gl4_build(const EpdWaveformPhases* source, uint8_t (*data)[16][4]) {
    if (!source || !source->luts || source->phase_times || !data ||
        source->phases != E0470_GL4_SOURCE_PHASES) return 0;
    int longest = 0;
    memset(data, 0, E0470_GL4_SOURCE_PHASES * sizeof(*data));
    for (int to = 0; to < 16; to += 5) {
        for (int from = 0; from < 16; ++from) {
            if (from == to) continue;
            uint8_t sequence[E0470_GL4_SOURCE_PHASES];
            for (int f = 0; f < source->phases; ++f) {
                int action = (source->luts[(f*16+to)*4+from/4] >> (6-2*(from%4))) & 3;
                if (action > 2) return 0;
                sequence[f] = (uint8_t)action;
            }
            int begin = 0, end = source->phases;
            while (begin < end && sequence[begin] == 0) ++begin;
            while (end > begin && sequence[end - 1] == 0) --end;
            const int drive = to == 15 ? 2 : 1;
            while (begin < end && sequence[begin] == 3 - drive) ++begin;
            int tail = begin;
            while (tail < end && sequence[tail] == drive) ++tail;
            if (tail == begin) return 0;
            const int run = tail - begin;
            const int keep = to == 0 || to == 15 ? run : (run * 3 + 3) / 4;
            const int length = keep + end - tail;
            if (length + 3 > E0470_GL4_SOURCE_PHASES) return 0;
            for (int f = 0; f < length; ++f) {
                const int action = f < keep ? drive : sequence[tail + f - keep];
                data[f][to][from / 4] |= (uint8_t)(action << (6 - 2 * (from % 4)));
            }
            if (length > longest) longest = length;
        }
    }
    return longest + 3;
}
