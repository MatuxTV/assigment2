#pragma once
#include "Config.h"
#include "Rtl.h"

// ============================================================
//  Button debounce FSM (one instance per button).
//
//      UP --raw--> PRESS_WAIT --stable N ticks--> DOWN   (pressed pulse)
//       ^             |!raw                        |!raw
//       +-------------+                            v
//       +<--stable N ticks-- RELEASE_WAIT <--------+
//                                |raw -> DOWN
//
//  in.raw   : physical level, TRUE = pressed (caller inverts the
//             active-low INPUT_PULLUP pin)
//  pressed  : one-cycle pulse on a clean press
//  held()   : Moore output, true while the button is down
// ============================================================
class ButtonFsm : public Fsm {
public:
  struct In { bool raw = false; } in;

  enum class S : uint8_t { UP, PRESS_WAIT, DOWN, RELEASE_WAIT };
  Reg<S>       state{S::UP};
  Reg<uint8_t> cnt{0};
  Pulse        pressed;

  bool held() const { return state.q == S::DOWN || state.q == S::RELEASE_WAIT; }

  void comb() override {
    switch (state.q) {
      case S::UP:
        if (in.raw) { state.d = S::PRESS_WAIT; cnt.d = 0; }
        break;
      case S::PRESS_WAIT:
        if (!in.raw)                         state.d = S::UP;
        else if (cnt.q + 1 >= DEBOUNCE_TICKS) { state.d = S::DOWN; pressed.d = true; }
        else                                 cnt.d = cnt.q + 1;
        break;
      case S::DOWN:
        if (!in.raw) { state.d = S::RELEASE_WAIT; cnt.d = 0; }
        break;
      case S::RELEASE_WAIT:
        if (in.raw)                          state.d = S::DOWN;
        else if (cnt.q + 1 >= DEBOUNCE_TICKS) state.d = S::UP;
        else                                 cnt.d = cnt.q + 1;
        break;
    }
  }

  void commit() override { state.commit(); cnt.commit(); pressed.commit(); }

  static constexpr uint8_t DEBOUNCE_TICKS = MS_TO_TICKS(DEBOUNCE_MS);
};
