#pragma once
#include "Config.h"
#include "Rtl.h"

// ============================================================
//  Controller FSM - the top-level state machine of the RSU.
//
//  VHDL equivalent: one entity with
//      type state_t is (IDLE, SLEEP, WORKING, PREALARM, ALARM);
//      signal state, next_state : state_t;
//  Moore outputs (functions of `state`) + a few Mealy pulses
//  (functions of state AND input, valid one cycle after the
//  transition).
//
//  Transitions
//    IDLE      --start-->                  WORKING
//    IDLE      --10 s with no start-->     SLEEP
//    SLEEP     --start (wake-up)-->        WORKING
//    WORKING   --object < D1-->            ALARM
//    WORKING   --object in [D1, D2]-->     PREALARM
//    PREALARM  --object < D1-->            ALARM
//    PREALARM  --sweep done, none < D2-->  WORKING
//    ALARM     --reset-->                  WORKING
//    WORKING|PREALARM|ALARM --stop-->      IDLE
// ============================================================

enum class RsuState : uint8_t {
  IDLE = 0, WORKING = 1, PREALARM = 2, ALARM = 3, SLEEP = 4
};  // values match proto::StateId

inline const char* stateName(RsuState s) {
  switch (s) {
    case RsuState::IDLE:     return "IDLE";
    case RsuState::WORKING:  return "WORKING";
    case RsuState::PREALARM: return "PREALARM";
    case RsuState::ALARM:    return "ALARM";
    default:                 return "SLEEP";
  }
}

enum class RadarMode : uint8_t {
  OFF,          // idle / sleep: servo parked, sonar off
  SWEEP_S1,     // working
  SWEEP_S2,     // pre-alarm (faster)
  MANUAL_POT,   // alarm: operator turns the potentiometer
  MANUAL_REMOTE // optional: console sends ANGLE,<deg>
};

enum class AlarmLed : uint8_t { OFF, ON, BLINK };

class ControllerFsm : public Fsm {
public:
  // ---------- inputs ("ports"), driven by the caller each cycle ----
  struct In {
    bool     start = false;   // debounced one-cycle pulses
    bool     stop  = false;
    bool     reset = false;   // button OR console RESET
    bool     remoteTake    = false;
    bool     remoteRelease = false;
    bool     meas      = false;   // a sector was just measured
    uint16_t measCm    = 0;       // 0 = no echo
    bool     sweepDone = false;   // one full pass finished
    uint16_t sweepMinCm = 0;      // nearest echo of that pass, 0 = none
  } in;

  // ---------- registered state ----------
  Reg<RsuState> state{RsuState::IDLE};
  Reg<bool>     remote{false};          // console owns the radar
  Reg<uint16_t> idleCnt{0};             // ticks spent in IDLE

  // ---------- pulses (Mealy) ----------
  Pulse stateChanged;   // -> CommsTask sends STATE,..
  Pulse radarReset;     // -> RadarFsm goes back to 0 deg, clears sectors

  // ---------- Moore outputs: pure functions of the state ----------
  bool onLed() const {
    RsuState s = state.q;
    return s != RsuState::IDLE && s != RsuState::SLEEP;
  }
  AlarmLed alarmLed() const {
    switch (state.q) {
      case RsuState::PREALARM: return AlarmLed::ON;
      case RsuState::ALARM:    return AlarmLed::BLINK;
      default:                 return AlarmLed::OFF;
    }
  }
  RadarMode radarMode() const {
    switch (state.q) {
      case RsuState::WORKING:  return remote.q ? RadarMode::MANUAL_REMOTE : RadarMode::SWEEP_S1;
      case RsuState::PREALARM: return remote.q ? RadarMode::MANUAL_REMOTE : RadarMode::SWEEP_S2;
      case RsuState::ALARM:    return remote.q ? RadarMode::MANUAL_REMOTE : RadarMode::MANUAL_POT;
      default:                 return RadarMode::OFF;
    }
  }
  bool deepSleep() const { return state.q == RsuState::SLEEP; }

  // ---------- the clocked process ----------
  void comb() override {
    const RsuState s = state.q;
    const bool active = (s == RsuState::WORKING ||
                         s == RsuState::PREALARM ||
                         s == RsuState::ALARM);

    // remote control ownership (only while the radar is running)
    if (active) {
      if (in.remoteTake)    remote.d = true;
      if (in.remoteRelease) remote.d = false;
    }

    // idle timer: counts only in IDLE, saturates
    if (s == RsuState::IDLE && idleCnt.q < 0xFFFF) idleCnt.d = idleCnt.q + 1;

    // classification of the current measurement
    const bool objAlarm = in.meas && in.measCm > 0 && in.measCm < D1_CM;
    const bool objPre   = in.meas && in.measCm >= D1_CM && in.measCm <= D2_CM;
    const bool nearInSweep = in.sweepMinCm > 0 && in.sweepMinCm <= D2_CM;

    switch (s) {
      case RsuState::IDLE:
        if (in.start)                              go(RsuState::WORKING, true);
        else if (idleCnt.q >= IDLE_TO_SLEEP_TICKS) go(RsuState::SLEEP, false);
        break;

      case RsuState::SLEEP:
        if (in.start) go(RsuState::WORKING, true);
        break;

      case RsuState::WORKING:
        if (in.stop)          go(RsuState::IDLE, false);
        else if (objAlarm)    go(RsuState::ALARM, false);
        else if (objPre)      go(RsuState::PREALARM, false);
        break;

      case RsuState::PREALARM:
        if (in.stop)                              go(RsuState::IDLE, false);
        else if (objAlarm)                        go(RsuState::ALARM, false);
        else if (in.sweepDone && !nearInSweep)    go(RsuState::WORKING, false);
        break;

      case RsuState::ALARM:
        if (in.stop)       go(RsuState::IDLE, false);
        else if (in.reset) go(RsuState::WORKING, true);   // radar back to 0 deg
        break;
    }
  }

  void commit() override {
    state.commit(); remote.commit(); idleCnt.commit();
    stateChanged.commit(); radarReset.commit();
  }

  static constexpr uint16_t IDLE_TO_SLEEP_TICKS = MS_TO_TICKS(IDLE_TO_SLEEP_MS);

private:
  // next-state assignment + the actions tied to a transition
  void go(RsuState next, bool resetRadar) {
    state.d = next;
    stateChanged.d = true;
    idleCnt.d = 0;
    if (resetRadar) radarReset.d = true;
    if (next == RsuState::IDLE || next == RsuState::SLEEP) remote.d = false;
  }
};
