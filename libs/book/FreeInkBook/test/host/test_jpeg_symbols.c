/* A platform may already export TJpgDec names with a different JDEC layout.
 * Keep those symbols in this binary and prove the book decoder stays private. */
#include <tjpgd.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static size_t empty_input(JDEC* decoder, uint8_t* buffer, size_t size) {
  (void)decoder; (void)buffer; (void)size;
  return 0;
}

int main(void) {
  JDEC decoder;
  uint32_t workspace[2048];
  assert(jd_prepare(&decoder, empty_input, workspace, sizeof(workspace), NULL) == JDR_INP);
  puts("JPEG decoder coexists with platform TJpgDec symbols");
  return 0;
}

#undef jd_prepare
#undef jd_decomp
JRESULT jd_prepare(JDEC* decoder, size_t (*input)(JDEC*, uint8_t*, size_t),
                   void* workspace, size_t size, void* device) {
  (void)decoder; (void)input; (void)workspace; (void)size; (void)device;
  abort();
}
JRESULT jd_decomp(JDEC* decoder, int (*output)(JDEC*, void*, JRECT*), uint8_t scale) {
  (void)decoder; (void)output; (void)scale;
  abort();
}
