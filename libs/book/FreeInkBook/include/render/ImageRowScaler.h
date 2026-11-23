#pragma once
#include "ImageRenderer.h"
#include <cstring>
namespace freeink::book {
// Maps completed source rows to output rows with box-filter averaging: every
// source pixel contributes to exactly one output pixel, so downscales keep
// tonal detail instead of sampling every Nth pixel (nearest-neighbor's
// aliasing reads as blur/jaggies on photos).
struct RowScaler {
  uint16_t srcW = 0;
  uint16_t srcH = 0;
  uint16_t dstW = 0;
  uint16_t dstH = 0;
  uint8_t* dstRow = nullptr;
  uint32_t* sums = nullptr;    // dstW accumulators
  uint32_t* counts = nullptr;  // dstW sample counts
  ImageRowFn rowFn = nullptr;
  void* user = nullptr;
  uint32_t nextDst = 0;
  bool stopped = false;
  bool failed = false;

  void resetAccum() {
    memset(sums, 0, static_cast<size_t>(dstW) * sizeof(uint32_t));
    memset(counts, 0, static_cast<size_t>(dstW) * sizeof(uint32_t));
  }

  bool emitAveraged() {
    for (uint32_t x = 0; x < dstW; ++x) {
      dstRow[x] = counts[x] != 0 ? static_cast<uint8_t>(sums[x] / counts[x]) : 0xFF;
    }
    if (!rowFn(user, static_cast<uint16_t>(nextDst), dstRow, dstW)) {
      stopped = true;
      return false;
    }
    ++nextDst;
    resetAccum();
    return true;
  }

  // Feeds one completed source row; emits output rows as their source bands
  // complete.
  bool feedSourceRow(uint32_t srcY, const uint8_t* gray) {
    // Emit any output rows whose source band ended before this row.
    while (nextDst < dstH &&
           srcY >= static_cast<uint64_t>(nextDst + 1) * srcH / dstH) {
      if (!emitAveraged()) return false;
    }
    if (nextDst >= dstH) return true;
    uint32_t dx=0,phase=0;
    for (uint32_t x = 0; x < srcW; ++x) {
      sums[dx] += gray[x];
      ++counts[dx];
      // Same floor(x*dstW/srcW) bins, without a 64-bit division per pixel.
      phase+=dstW;
      while (phase>=srcW) { phase-=srcW; ++dx; }
    }
    // Last source row of the image: flush the final band.
    if (srcY + 1 == srcH) {
      while (nextDst < dstH) {
        if (!emitAveraged()) return false;
      }
    }
    return true;
  }
};

inline bool initScaler(RowScaler& scaler, Arena& scratch) {
  scaler.dstRow = static_cast<uint8_t*>(scratch.alloc(scaler.dstW, 1));
  scaler.sums = scratch.allocArray<uint32_t>(scaler.dstW);
  scaler.counts = scratch.allocArray<uint32_t>(scaler.dstW);
  if (scaler.dstRow == nullptr || scaler.sums == nullptr || scaler.counts == nullptr) {
    return false;
  }
  scaler.resetAccum();
  return true;
}

}
