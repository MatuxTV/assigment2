#pragma once
#include <stdint.h>

// ------------------------------------------------------------
//  Base class of every periodic task.
//
//  Contract:
//    * tick() must return immediately - no delay(), no while(),
//      no pulseIn().  Anything that takes time is split into
//      phases of a small internal FSM.
//    * tick() is called by the Scheduler at a fixed period,
//      which is the "clock" of the synchronous FSM.
// ------------------------------------------------------------
class Task {
public:
  virtual void begin(uint32_t now) { (void)now; }
  virtual void tick(uint32_t now) = 0;
  virtual ~Task() {}
};
