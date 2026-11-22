#pragma once
#include <cstdio>
#define esp_rom_printf(...) std::fprintf(stderr, __VA_ARGS__)
