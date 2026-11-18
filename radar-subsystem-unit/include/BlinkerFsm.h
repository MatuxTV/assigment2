#pragma once
#include "Config.h"
#include "Rtl.h"
#include "ControllerFsm.h"   // AlarmLed

// ============================================================
//  LED driver for the ALARM led.
//  mode OFF   -> out = 0
//  mode ON    -> out = 1
//  mode BLINK -> out toggles every ALARM_BLINK_MS
//  `out` is a registered (glitch-free) output pin value.
// ============================================================
class BlinkerFsm : public Fsm {
public:
  struct In { AlarmLed mode = AlarmLed::OFF; } in;

  Reg<bool>     out{false};
  Reg<uint16_t> cnt{0};

  void comb() override {
    switch (in.mode) {
      case AlarmLed::OFF: out.d = false; cnt.d = 0; break;
      case AlarmLed::ON:  out.d = true;  cnt.d = 0; break;
      case AlarmLed::BLINK:
        if (cnt.q + 1 >= HALF_PERIOD_TICKS) { cnt.d = 0; out.d = !out.q; }
        else                                { cnt.d = cnt.q + 1; }
        break;
    }
  }

  void commit() override { out.commit(); cnt.commit(); }

  static constexpr uint16_t HALF_PERIOD_TICKS = MS_TO_TICKS(ALARM_BLINK_MS);
};
