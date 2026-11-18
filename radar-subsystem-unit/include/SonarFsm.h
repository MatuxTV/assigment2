#pragma once
#include "Config.h"
#include "Rtl.h"

// ============================================================
//  HC-SR04 measurement FSM (non-blocking, no pulseIn()).
//
//      IDLE --req--> TRIG --> WAIT --echo | timeout--> IDLE
//                    trig=1   (done pulse)
//
//  Hardware glue (in the .ino):
//    * trigPin  <= sonar.trig.q               (Moore: state == TRIG)
//    * the echo ISR measures the pulse width; while `armEcho()`
//      is true the .ino clears its "captured" flag, afterwards it
//      feeds in.echoValid / in.echoUs.
//    * this is the usual VHDL pattern: an asynchronous input is
//      captured by an ISR and enters the FSM as a synchronised
//      level.
//
//  in.req       : pulse, start one measurement
//  in.echoValid : echo width captured since the trigger
//  in.echoUs    : width in microseconds
//  in.tempTenths: air temperature, calibrates the speed of sound
//
//  done   : pulse, `cm` is valid (0 = nothing in range)
// ============================================================

// speed of sound [cm/us], v = 331.3 + 0.606 * T  [m/s]
inline float soundCmPerUs(float celsius) {
  return (331.3f + 0.606f * celsius) / 10000.0f;
}

class SonarFsm : public Fsm {
public:
  struct In {
    bool     req = false;
    bool     echoValid = false;
    uint32_t echoUs = 0;
    int16_t  tempTenths = 200;
  } in;

  enum class S : uint8_t { IDLE, TRIG, WAIT };
  Reg<S>        state{S::IDLE};
  Reg<uint16_t> cnt{0};
  Reg<uint16_t> cm{0};
  Pulse         done;

  bool trig()    const { return state.q == S::TRIG; }
  bool armEcho() const { return state.q == S::TRIG; }
  bool busy()    const { return state.q != S::IDLE; }

  static uint16_t toCm(uint32_t echoUs, int16_t tempTenths) {
    if (echoUs == 0) return 0;
    float c = (float)echoUs * soundCmPerUs(tempTenths / 10.0f) / 2.0f;
    if (c < 1.0f || c > (float)SONAR_MAX_CM) return 0;
    return (uint16_t)(c + 0.5f);
  }

  void comb() override {
    switch (state.q) {
      case S::IDLE:
        if (in.req) { state.d = S::TRIG; cnt.d = 0; }
        break;
      case S::TRIG:                       // trig held high for one tick (>> 10 us)
        state.d = S::WAIT; cnt.d = 0;
        break;
      case S::WAIT:
        if (in.echoValid) {
          cm.d = toCm(in.echoUs, in.tempTenths);
          done.d = true; state.d = S::IDLE;
        } else if (cnt.q + 1 >= TIMEOUT_TICKS) {
          cm.d = 0; done.d = true; state.d = S::IDLE;
        } else {
          cnt.d = cnt.q + 1;
        }
        break;
    }
  }

  void commit() override { state.commit(); cnt.commit(); cm.commit(); done.commit(); }

  static constexpr uint16_t TIMEOUT_TICKS = MS_TO_TICKS(MEASURE_TIMEOUT_MS);
};
