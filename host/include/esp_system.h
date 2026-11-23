#pragma once
#include "Arduino.h"
inline int esp_reset_reason() { return 1; }
inline void esp_restart() { ESP.restart(); }
