#include <FlashAccess.h>
#if defined(ESP_PLATFORM) && FREEINK_DEVICE_METALIO_EINK4_PLUS
#include <ResourceCoordinator.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace {
DRAM_ATTR bool coordinated=false;
}
extern "C" void __real_spi_flash_disable_interrupts_caches_and_other_cpu();
extern "C" void __real_spi_flash_enable_interrupts_caches_and_other_cpu();
extern "C" void IRAM_ATTR __wrap_spi_flash_disable_interrupts_caches_and_other_cpu() {
  if(coordinated)while(!freeink::resources::Coordinator::get().beginFlash())vTaskDelay(1);
  __real_spi_flash_disable_interrupts_caches_and_other_cpu();
  // Cache is off here: only the IRAM function epilogue may execute.
}
extern "C" void IRAM_ATTR __wrap_spi_flash_enable_interrupts_caches_and_other_cpu() {
  __real_spi_flash_enable_interrupts_caches_and_other_cpu();
  if(coordinated)freeink::resources::Coordinator::get().end(freeink::resources::Coordinator::Client::Flash);
}
namespace freeink::resources {void coordinateFlashAccess(){Coordinator::get();coordinated=true;}}
#else
namespace freeink::resources {void coordinateFlashAccess(){}}
#endif
