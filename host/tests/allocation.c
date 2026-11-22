#include <stdlib.h>
void* c_allocation(size_t n) { return malloc(n); }
void c_release(void* p) { free(p); }
