// Standalone S31 diagnostic: never mounts or writes user storage.
#include <Arduino.h>
#include <esp_async_memcpy.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <cstring>

static bool completed(async_memcpy_handle_t,async_memcpy_event_t*,void* context) {
  BaseType_t awakened=pdFALSE;
  xSemaphoreGiveFromISR(static_cast<SemaphoreHandle_t>(context),&awakened);
  return awakened==pdTRUE;
}
void setup() {
  Serial.begin(115200); delay(2500);
  constexpr size_t capacity=1216*684;
  constexpr auto caps=MALLOC_CAP_SPIRAM|MALLOC_CAP_DMA|MALLOC_CAP_8BIT;
  auto* source=static_cast<uint8_t*>(heap_caps_aligned_alloc(128,capacity,caps));
  auto* destination=static_cast<uint8_t*>(heap_caps_aligned_alloc(128,capacity,caps));
  auto signal=xSemaphoreCreateBinary();
  async_memcpy_config_t configuration=ASYNC_MEMCPY_DEFAULT_CONFIG();
  configuration.backlog=1; configuration.dma_burst_size=64;
  async_memcpy_handle_t dma=nullptr;
  const auto installed=esp_async_memcpy_install(&configuration,&dma);
  Serial.printf("COPY_BEGIN cpu_mhz=%u psram=%u install=%d\n",getCpuFrequencyMhz(),unsigned(ESP.getPsramSize()),installed);
  if (!source || !destination || !signal || installed!=ESP_OK) { Serial.println("COPY_ALLOCATION_FAILED"); return; }
  const size_t sizes[]={64,512,4096,65536,262144,capacity};
  for (auto size:sizes) for (unsigned trial=0;trial<10;++trial) {
    for (unsigned method=0;method<2;++method) {
      const bool hardware=(method+trial)%2;
      for (size_t i=0;i<size;++i) source[i]=uint8_t(i*17+trial);
      memset(destination,0x55,size);
      auto started=esp_timer_get_time();
      esp_err_t status=ESP_OK;
      if (hardware) {
        // The official driver flushes source cache lines and synchronizes the
        // destination. Include descriptor setup and completion in the timing.
        status=esp_async_memcpy(dma,destination,source,size,completed,signal);
        if (status==ESP_OK && xSemaphoreTake(signal,pdMS_TO_TICKS(2000))!=pdTRUE) {
          Serial.println("COPY_TIMEOUT_BUFFERS_RETAINED"); return;
        }
      } else memcpy(destination,source,size);
      const auto elapsed=esp_timer_get_time()-started;
      const bool equal=status==ESP_OK && !memcmp(source,destination,size);
      Serial.printf("COPY bytes=%u trial=%u dma=%u us=%u status=%d equal=%u\n",unsigned(size),trial,hardware,unsigned(elapsed),status,equal);
      delay(2);
    }
  }
  esp_async_memcpy_uninstall(dma); vSemaphoreDelete(signal);
  free(source); free(destination); Serial.println("COPY_DONE");
}
void loop() { delay(1000); }
