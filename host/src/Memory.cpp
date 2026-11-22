#include "HostMemory.h"
#include "esp_heap_caps.h"
#include <cstdlib>
namespace freeink::host {
thread_local bool firmwareAllocations=false;
void configureResources(const Json&) {}
Json resourceStats(){return Json(Object{{"enabled",Json(false)}});}
void cpuCheckpoint(){}
void ioCost(size_t){}
}
extern "C" {
void* host_malloc(size_t n){return std::malloc(n);}
void* host_calloc(size_t n,size_t s){return std::calloc(n,s);}
void* host_realloc(void* p,size_t n){return std::realloc(p,n);}
void host_free(void* p){std::free(p);}
void* host_caps_malloc(size_t n,unsigned){return std::malloc(n);}
void* host_caps_realloc(void* p,size_t n,unsigned){return std::realloc(p,n);}
size_t host_heap_free(unsigned){return 16*1024*1024;}
size_t host_heap_largest(unsigned c){return host_heap_free(c);}
}
