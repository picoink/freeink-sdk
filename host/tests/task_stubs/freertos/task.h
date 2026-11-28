#pragma once
#include "FreeRTOS.h"
inline bool taskCreateFails=false,taskDeleted=false;
inline TaskHandle_t createdTask=nullptr,runningTask=nullptr;
struct TaskSuspended {};
inline BaseType_t xPortGetCoreID(){return 0;}
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return runningTask;}
inline size_t uxTaskGetStackHighWaterMark(TaskHandle_t){return 8192;}
inline TaskHandle_t xTaskCreateStaticPinnedToCore(void(*fn)(void*),const char*,size_t,void* arg,unsigned,StackType_t*,StaticTask_t* tcb,BaseType_t) {
  if(taskCreateFails)return nullptr;
  tcb->function=fn;tcb->argument=arg;taskDeleted=false;return createdTask=tcb;
}
inline void vTaskSuspend(TaskHandle_t task){if(!task)throw TaskSuspended{};}
inline void vTaskDelete(TaskHandle_t task){assert(task!=runningTask);taskDeleted=true;}
inline void runCreatedTask(){
  runningTask=createdTask;
  try{createdTask->function(createdTask->argument);assert(false);}catch(const TaskSuspended&){}
  runningTask=nullptr;
}
