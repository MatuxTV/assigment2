#pragma once
#include <stdint.h>
#include "Task.h"

// ------------------------------------------------------------
//  Cooperative, non-blocking periodic scheduler.
//
//  This is the "clock" of the synchronous FSMs: every task is
//  called at a fixed period and must return immediately.
//
//  The comparison (now - last) is done in unsigned arithmetic,
//  so it stays correct across the millis() rollover (49 days).
//
//  Order of the slots matters: the ControllerTask must be the
//  LAST one, so that it sees every event produced in the same
//  scheduler pass.
// ------------------------------------------------------------
class Scheduler {
public:
  struct Slot {
    Task*    task;
    uint16_t periodMs;
    uint32_t last;
  };

  Scheduler(Slot* slots, uint8_t count) : slots_(slots), count_(count) {}

  void begin(uint32_t now) {
    for (uint8_t i = 0; i < count_; ++i) {
      slots_[i].last = now;
      slots_[i].task->begin(now);
    }
  }

  void run(uint32_t now) {
    for (uint8_t i = 0; i < count_; ++i) {
      Slot& s = slots_[i];
      if ((uint32_t)(now - s.last) >= s.periodMs) {
        s.last = now;
        s.task->tick(now);
      }
    }
  }

private:
  Slot*   slots_;
  uint8_t count_;
};
