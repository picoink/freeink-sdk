#include "e0470_gl4.h"
#include "my_waveform_gl.h"
#include <assert.h>
int main(void) {
 uint8_t output[48][16][4],source[sizeof(my_gl_25_0_data)];
 memcpy(source,my_gl_25_0_data,sizeof(source));
 assert(e0470_gl4_build(&my_gl_25_0,output)==24);
 assert(!memcmp(source,my_gl_25_0_data,sizeof(source)));
 for(int p=0;p<48;++p)for(int to=0;to<16;++to)for(int from=0;from<16;++from) {
  int action=(output[p][to][from/4]>>(6-2*(from%4)))&3;
  assert(action<=2);if(to%5 || p>=21 || to==from)assert(!action);
 }
 EpdWaveformPhases bad=my_gl_25_0;bad.phases=47;assert(!e0470_gl4_build(&bad,output));
 bad=my_gl_25_0;int times[48];bad.phase_times=times;assert(!e0470_gl4_build(&bad,output));
 assert(!e0470_gl4_build(0,output));return 0;
}
