#pragma once
#include <ResourceMemory.h>
#include <InternalMemory.h>
#include <atomic>
#if defined(ESP_PLATFORM) && !defined(FREEINK_HOST)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace freeink::resources {
// Bounded task-side work without an internal stack. The creator must reap on
// the same core; callbacks must delegate flash writes to an internal-stack
// worker. Completion means all callback locals have already been destroyed.
class ExternalTask {
 public:
  ExternalTask()=default;
  ExternalTask(const ExternalTask&)=delete;
  ExternalTask& operator=(const ExternalTask&)=delete;
  ~ExternalTask(){reset();}
  bool start(const char* name,size_t bytes,void(*function)(void*),void* argument) {
    if(task_ || !function || bytes>UINT32_MAX)return false;
    stack_=static_cast<StackType_t*>(Memory::get().allocate(bytes,Category::System,0,Lifetime::Operation));
    if(!stack_)return false;
    function_=function;argument_=argument;finished_=false;core_=xPortGetCoreID();
    const bool ok=InternalMemory::get().createTask(sizeof(StaticTask_t),[&]{
      control_=static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
      if(!control_)return false;
      task_=xTaskCreateStaticPinnedToCore([](void* p){
        auto& self=*static_cast<ExternalTask*>(p);
        self.function_(self.argument_);
        self.finished_=true;
        for(;;)vTaskSuspend(nullptr);
      },name,bytes,this,1,stack_,control_,core_);
      return task_!=nullptr;
    });
    if(!ok)reset();
    return ok;
  }
  bool finished()const{return finished_.load();}
  size_t stackFree()const{return task_?uxTaskGetStackHighWaterMark(task_):0;}
  void reset() {
    if(task_) {
      // A running callback may own locks. Only its creator can reap a completed
      // task; pinning to that same core makes deletion synchronous in FreeRTOS.
      configASSERT(finished_ && xPortGetCoreID()==core_ && task_!=xTaskGetCurrentTaskHandle());
      vTaskSuspend(task_);vTaskDelete(task_);task_=nullptr;
    }
    heap_caps_free(control_);control_=nullptr;
    Memory::get().release(stack_,0);stack_=nullptr;
  }
 private:
  TaskHandle_t task_=nullptr;StaticTask_t* control_=nullptr;StackType_t* stack_=nullptr;
  void(*function_)(void*)=nullptr;void* argument_=nullptr;
  std::atomic<bool> finished_{false};BaseType_t core_=0;
};
}
#endif
