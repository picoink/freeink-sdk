#undef FREEINK_HOST
#define ESP_PLATFORM 1
#define FREEINK_TASK_TEST 1
#include <ExternalTask.h>
#include <HostRuntime.h>
#include <cassert>
int main(){
  using namespace freeink::resources;
  freeink::host::Json config;
  assert(freeink::host::parseJson(R"({"memory":{"ram_bytes":65536,"psram_bytes":16777216}})",config).empty());
  freeink::host::configureResources(config);
  const auto psram=heap_caps_get_free_size(MALLOC_CAP_SPIRAM),internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  auto& memory=Memory::get();const auto before=memory.snapshot().current;
  ExternalTask task;bool ran=false;
  auto callback=[](void* p){*static_cast<bool*>(p)=true;};
  assert(task.start("test",16384,callback,&ran) && !ran && !task.finished());
  assert(!task.start("duplicate",16384,callback,&ran));
  assert(memory.snapshot().categories[size_t(Category::System)].current>=16384);
  assert(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)>internal-1024);
  runCreatedTask();assert(ran && task.finished());task.reset();assert(taskDeleted);
  assert(memory.snapshot().current==before && heap_caps_get_free_size(MALLOC_CAP_SPIRAM)==psram && heap_caps_get_free_size(MALLOC_CAP_INTERNAL)==internal);
  taskCreateFails=true;assert(!task.start("failure",16384,callback,&ran));taskCreateFails=false;
  assert(memory.snapshot().current==before && heap_caps_get_free_size(MALLOC_CAP_INTERNAL)==internal);
  // An optional task cannot consume the protected internal reserve even though
  // its stack fits in PSRAM. Both failed admissions must roll back that stack.
  void* pressure=heap_caps_malloc(internal-16384,MALLOC_CAP_INTERNAL);assert(pressure);
  assert(!task.start("reserve",16384,callback,&ran));heap_caps_free(pressure);
  assert(memory.snapshot().current==before && heap_caps_get_free_size(MALLOC_CAP_SPIRAM)==psram);
  assert(!task.start("quota",4*MiB,callback,&ran) && memory.snapshot().current==before);
}
