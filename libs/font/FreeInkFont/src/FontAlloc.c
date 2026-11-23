#include "FontAlloc.h"

#include <stdlib.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"

// Internal RAM is a compatibility path only for boards without PSRAM. A full
// external heap must not consume memory reserved for DMA and system threads.
static unsigned font_caps(void) {
  return (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL)
      | MALLOC_CAP_8BIT;
}
void* fiFontMalloc(size_t size) {
  return heap_caps_malloc(size, font_caps());
}

void* fiFontRealloc(void* ptr, size_t size) {
  return heap_caps_realloc(ptr, size, font_caps());
}

void fiFontFree(void* ptr) { heap_caps_free(ptr); }

#else  // host / non-ESP build

void* fiFontMalloc(size_t size) { return malloc(size); }
void* fiFontRealloc(void* ptr, size_t size) { return realloc(ptr, size); }
void fiFontFree(void* ptr) { free(ptr); }

#endif
