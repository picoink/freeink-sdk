#pragma once
#include <cstdint>
namespace BoardConfig {
struct Profile { uint16_t displayWidth = 16, displayHeight = 8; };
inline Profile ACTIVE;
}
