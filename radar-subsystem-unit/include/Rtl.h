#pragma once
#include <stdint.h>

// ============================================================
//  Tiny "RTL" layer: the C++ equivalent of VHDL clocked logic.
//
//  VHDL                         C++
//  ------------------------     ----------------------------
//  signal s : T;                Reg<T> s;
//  s <= value;   (next value)   s.d = value;
//  reading s     (current)      s.q
//  rising_edge(clk) s <= s_next Reg::commit()
//  process(all) -- comb logic   Fsm::comb()
//
//  Rules (same as in hardware):
//    1. comb() reads ONLY `.q` and writes ONLY `.d`.
//    2. Nothing changes `.q` except commit(), which the Clock
//       calls for all registers at the same instant.
//    => the order of the FSMs never matters.
//
//  Default assignment: after commit() `.d == .q`, so a register
//  that is not written this cycle simply HOLDS its value. No
//  latches, no forgotten `else`.
// ============================================================

// A register: q = value seen by everybody, d = value it will take.
template <typename T>
struct Reg {
  T q;
  T d;
  explicit Reg(T init = T()) : q(init), d(init) {}
  void commit() { q = d; }
};

// A one-cycle strobe: d defaults to false every cycle, so it is
// high for exactly one clock period after comb() sets it.
struct Pulse {
  bool q = false;
  bool d = false;
  void commit() { q = d; d = false; }
};

// Clock-enable generator: pulses once every N clock ticks.
// (VHDL: a counter that wraps and drives a `ce` signal.)
class Strobe {
public:
  explicit Strobe(uint16_t period) : period_(period) {}
  void comb()   { cnt_.d = (cnt_.q + 1 >= period_) ? 0 : cnt_.q + 1; }
  void commit() { cnt_.commit(); }
  bool ce() const { return cnt_.q == 0; }
private:
  uint16_t        period_;
  Reg<uint16_t>   cnt_{0};
};

// ------------------------------------------------------------
//  Anything driven by the clock.
// ------------------------------------------------------------
class Fsm {
public:
  virtual void comb()   = 0;   // next-state / output logic
  virtual void commit() = 0;   // the clock edge: q <= d
  virtual ~Fsm() {}
};

// ------------------------------------------------------------
//  The clock. One tick() == one rising edge.
//
//    wiring (done by the caller, concurrent assignments)
//      -> comb()   of every FSM   (compute all `d`)
//      -> commit() of every FSM   (all `q <= d` at once)
//
//  advance() turns the free-running millis() into clean edges
//  with no drift: it catches up if loop() was ever late.
// ------------------------------------------------------------
class Clock {
public:
  Clock(Fsm** fsms, uint8_t count, uint16_t periodMs)
      : fsms_(fsms), count_(count), periodMs_(periodMs) {}

  void begin(uint32_t nowMs) { last_ = nowMs; }

  // returns true when an edge is due; caller then wires + tick().
  bool due(uint32_t nowMs) const {
    return (uint32_t)(nowMs - last_) >= periodMs_;
  }

  void tick() {
    last_ += periodMs_;
    for (uint8_t i = 0; i < count_; ++i) fsms_[i]->comb();
    for (uint8_t i = 0; i < count_; ++i) fsms_[i]->commit();
  }

private:
  Fsm**    fsms_;
  uint8_t  count_;
  uint16_t periodMs_;
  uint32_t last_ = 0;
};
