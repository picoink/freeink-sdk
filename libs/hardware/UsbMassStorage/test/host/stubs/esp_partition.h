#pragma once
struct esp_partition_t {};
constexpr int ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_SUBTYPE_DATA_FAT = 0x81;
const esp_partition_t* esp_partition_find_first(int,int,const char*);
