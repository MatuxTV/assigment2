#pragma once
#include <stdint.h>
#include "Config.h"

// ------------------------------------------------------------
//  Shared radar picture.
//
//  Written by RadarTask, read by DisplayTask and CommsTask.
//  Keeping it in one place means the LCD and the console can
//  never disagree about what the radar currently sees.
// ------------------------------------------------------------
struct SectorData {
  uint16_t cm       = 0;      // 0 = nothing in range
  bool     detected = false;
  uint32_t at       = 0;      // millis() of the last update
};

struct RadarModel {
  SectorData sectors[SECTOR_COUNT];
  uint8_t    currentSector = 0;
  uint8_t    currentAngle  = 0;
  int8_t     direction     = 1;   // +1 forward, -1 backward

  void reset() {
    for (uint8_t i = 0; i < SECTOR_COUNT; ++i) sectors[i] = SectorData{};
    currentSector = 0;
    currentAngle  = 0;
    direction     = 1;
  }

  void store(uint8_t sector, uint16_t cm, bool detected, uint32_t now) {
    if (sector >= SECTOR_COUNT) return;
    sectors[sector].cm       = cm;
    sectors[sector].detected = detected;
    sectors[sector].at       = now;
  }
};
