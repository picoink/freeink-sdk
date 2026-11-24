#pragma once
#include <ResourceMemory.h>
#ifndef FREEINK_HOST
#include <mbedtls/platform.h>
#endif
namespace freeink::resources {
// Install during board setup, before any network/TLS client is constructed.
// TLS library state may be shared across connections, so its allocations are
// system-owned but compete for the single transient quota, never internal SRAM.
struct TlsMemory {
  static void* calloc(size_t count,size_t bytes) {
    if(bytes && count>SIZE_MAX/bytes)return nullptr;
    const auto size=count*bytes;auto* p=Memory::get().allocate(size,Category::Transient,0,Lifetime::Operation);
    if(p)memset(p,0,size);return p;
  }
  static void free(void* p){Memory::get().release(p);}
  static bool configure() {
#ifndef FREEINK_HOST
    static const bool configured=mbedtls_platform_set_calloc_free(calloc,free)==0;return configured;
#else
    return true;
#endif
  }
};
}
