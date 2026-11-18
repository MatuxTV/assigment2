#pragma once
#include <stdint.h>

// ------------------------------------------------------------
//  One sector measurement produced by the RadarTask.
// ------------------------------------------------------------
struct SectorResult {
  uint8_t  sector;    // 0 .. SECTOR_COUNT-1
  uint16_t cm;        // 0 = no echo within range
  bool     detected;  // cm > 0 && cm <= SONAR_MAX_CM
};

// ------------------------------------------------------------
//  Event bus.
//
//  Tasks WRITE flags here, the Controller FSM READS them and
//  clears the whole struct at the end of every control tick.
//  This is the C++ equivalent of the VHDL "signal" that is
//  valid for exactly one clock edge.
// ------------------------------------------------------------
struct Events {
  // --- from ButtonTask / CommsTask ---
  bool startPressed = false;
  bool stopPressed  = false;
  bool resetPressed = false;

  // --- from RadarTask ---
  bool         sectorReady = false;
  SectorResult sector{};
  bool         sweepDone   = false;  // one full pass finished
  uint16_t     sweepMinCm  = 0;      // min distance of that pass (0 = none)

  // --- from CommsTask (optional remote control) ---
  bool    remoteTake    = false;
  bool    remoteRelease = false;
  bool    remoteAngle   = false;
  uint8_t remoteAngleDeg = 0;

  void clear() { *this = Events{}; }
};
