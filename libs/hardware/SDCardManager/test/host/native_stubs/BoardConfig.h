#pragma once
#include <cstdint>
#define FREEINK_SD_SDMMC 1
#define FREEINK_MCU_S31 1
namespace BoardConfig {
struct SdmmcPins { int8_t clk, cmd, d0, d1, d2, d3; uint8_t busWidth; };
struct Profile { struct { int8_t powerEnable = -1; bool powerCycleOnMount = false, powerActiveHigh = true; } sd; };
inline Profile ACTIVE;
}
