#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
using BaseType_t=int;
using StackType_t=uint8_t;
struct StaticTask_t {void(*function)(void*)=nullptr;void* argument=nullptr;};
using TaskHandle_t=StaticTask_t*;
#define configASSERT(x) assert(x)
