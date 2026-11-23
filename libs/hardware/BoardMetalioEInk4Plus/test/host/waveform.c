#include "my_waveform.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
static void digest(const EpdWaveform* waveform) {
    assert(waveform);
    for (int m = 0; m < waveform->num_modes; ++m) {
        const EpdWaveformPhases* p = waveform->mode_data[m]->range_data[0];
        assert(p && p->luts && p->phases > 0);
        uint32_t hash = 2166136261u;
        for (int i = 0; i < p->phases * 64; ++i) hash = (hash ^ p->luts[i]) * 16777619u;
        printf("%d:%d:%08x\n", waveform->mode_data[m]->type, p->phases, hash);
    }
}
int main(void) {
    my_waveform_init();
    my_waveform_init();
    digest(&MY_WAVEFORM);
    digest(my_waveform_fast());
    digest(my_waveform_short_du());
    digest(my_waveform_fast_du());
    digest(my_waveform_gray8());
    digest(my_waveform_wipe_reveal());
    digest(my_waveform_wipe_reveal_short());
    for (int p = 0; p < 4; ++p) digest(my_waveform_wipe_gl(p));
    // The full-quality white transition must never drive toward black during
    // its erase phase, for any previous or target grayscale value.
    const EpdWaveformPhases* white = my_waveform_wipe_gl(0)->mode_data[0]->range_data[0];
    assert(white->phases == 68);
    for (int f = 0; f < 20; ++f) for (int to = 0; to < 16; ++to) for (int old = 0; old < 16; ++old) {
        const int action = (white->luts[(f*16+to)*4+old/4] >> (6-2*(old%4))) & 3;
        assert(action == 0 || action == 2);
    }
}
