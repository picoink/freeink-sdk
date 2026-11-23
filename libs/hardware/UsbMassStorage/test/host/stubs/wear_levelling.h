#pragma once
#include <cstddef>
#include "esp_partition.h"
using wl_handle_t = int;
constexpr int WL_INVALID_HANDLE = -1, ESP_OK = 0;
int wl_mount(const esp_partition_t*,wl_handle_t*);
int wl_unmount(wl_handle_t);
size_t wl_sector_size(wl_handle_t);
size_t wl_size(wl_handle_t);
int wl_read(wl_handle_t,size_t,void*,size_t);
int wl_write(wl_handle_t,size_t,const void*,size_t);
int wl_erase_range(wl_handle_t,size_t,size_t);
