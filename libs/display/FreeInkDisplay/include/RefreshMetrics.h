#pragma once
#include <cstdint>
namespace freeink {
// Last completed refresh. Zero counters mean the backend does not report them.
// Read only after refreshBusy() is false; units are microseconds unless named.
struct RefreshMetrics {
  uint32_t queue_us = 0;
  uint32_t completion_us = 0;
  uint32_t power_us = 0;
  uint32_t highlevel_us = 0;
  uint32_t worker_us = 0;
  uint32_t cycle_us = 0;
  uint32_t scan_us = 0;
  uint32_t lut_us = 0;
  uint32_t feed_wait_us = 0;
  uint32_t phase_avg_us = 0;
  uint32_t phase_min_us = 0;
  uint32_t phase_max_us = 0;
  uint32_t eof_gap_us = 0;
  uint32_t eof_work_us = 0;
  uint32_t phases = 0;
  uint32_t completed = 0;
  uint32_t faults = 0;
  uint32_t fault_phase = 0;
  uint32_t fault_line = 0;
  uint32_t fault_thread = 0;

  uint32_t pixel_clock_hz = 0;
};
}
