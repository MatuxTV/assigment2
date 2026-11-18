#pragma once
#include "Config.h"
#include "Rtl.h"
#include "ControllerFsm.h"   // RadarMode

// ============================================================
//  Radar sweep FSM: positions the servo sector by sector and
//  asks the sonar for a measurement in each one.
//
//   PARKED --mode != OFF--> HOME --settled--> MOVE --settled--> MEASURE
//     ^ (mode OFF from any state)               ^                  |
//     |                                         +--- sonar done ---+
//     +--- reset (from controller): -> HOME
//
//  Sweep modes   : sector advances 0..N-1, then N-1..0 (forward
//                  and backward). Settle time = S1 or S2 speed.
//  Manual modes  : the servo follows in.manualAngle; the sector is
//                  derived from the angle and still measured, so
//                  the console keeps getting data.
//
//  in.mode        : Moore output of the Controller
//  in.reset       : Controller.radarReset pulse -> back to 0 deg
//  in.manualAngle : angle chosen by the caller (pot or console)
//  in.sonarDone / in.sonarCm : from SonarFsm
//
//  outputs
//    angle        : servo target (0..180)
//    servoOn()    : false while PARKED (detach / power down servo)
//    sonarReq     : pulse, start a measurement
//    sectorReady  : pulse, outSector/outCm hold a fresh result
//    sweepDone    : pulse, sweepMin = nearest echo of that pass (0 = none)
// ============================================================
class RadarFsm : public Fsm {
public:
  struct In {
    RadarMode mode = RadarMode::OFF;
    bool      reset = false;
    uint8_t   manualAngle = 90;
    bool      sonarDone = false;
    uint16_t  sonarCm = 0;
  } in;

  enum class S : uint8_t { PARKED, HOME, MOVE, MEASURE };

  Reg<S>        state{S::PARKED};
  Reg<uint16_t> cnt{0};
  Reg<uint8_t>  sector{0};
  Reg<int8_t>   dir{1};
  Reg<uint8_t>  angle{0};
  Reg<uint16_t> passMin{0};     // nearest echo so far in this pass

  Pulse         sonarReq;
  Pulse         sectorReady;
  Reg<uint8_t>  outSector{0};
  Reg<uint16_t> outCm{0};
  Pulse         sweepDone;
  Reg<uint16_t> sweepMin{0};

  bool servoOn() const { return state.q != S::PARKED; }

  void comb() override {
    const bool manual = (in.mode == RadarMode::MANUAL_POT ||
                         in.mode == RadarMode::MANUAL_REMOTE);

    if (in.mode == RadarMode::OFF) { park(); return; }

    if (in.reset) { goHome(); return; }

    switch (state.q) {
      case S::PARKED:
        goHome();
        break;

      case S::HOME:                                   // wait at 0 deg
        angle.d = ANGLE_MIN;
        if (cnt.q + 1 >= HOME_TICKS) { state.d = S::MOVE; cnt.d = 0; }
        else                         { cnt.d = cnt.q + 1; }
        break;

      case S::MOVE: {
        uint8_t target;
        if (manual) {
          target = in.manualAngle > ANGLE_MAX ? ANGLE_MAX : in.manualAngle;
          sector.d = sectorForAngle(target);
        } else {
          target = angleForSector(sector.q);
        }
        angle.d = target;
        if (cnt.q + 1 >= settleTicks()) {            // servo has arrived
          state.d = S::MEASURE; cnt.d = 0; sonarReq.d = true;
        } else {
          cnt.d = cnt.q + 1;
        }
        break;
      }

      case S::MEASURE: {
        bool got = in.sonarDone;
        uint16_t cm = in.sonarCm;
        if (!got && cnt.q + 1 >= WATCHDOG_TICKS) { got = true; cm = 0; }   // sonar never answered
        if (!got) { cnt.d = cnt.q + 1; break; }

        // publish this sector
        outSector.d = sector.q;
        outCm.d     = cm;
        sectorReady.d = true;
        cnt.d   = 0;
        state.d = S::MOVE;

        if (manual) break;                            // no pass bookkeeping

        uint16_t m = passMin.q;
        if (cm > 0 && (m == 0 || cm < m)) m = cm;

        int16_t next = (int16_t)sector.q + dir.q;
        if (next < 0 || next >= SECTOR_COUNT) {       // end of a pass
          sweepDone.d = true;
          sweepMin.d  = m;
          passMin.d   = 0;
          dir.d       = -dir.q;
          sector.d    = (uint8_t)((int16_t)sector.q - dir.q);   // step back inside
        } else {
          passMin.d = m;
          sector.d  = (uint8_t)next;
        }
        break;
      }
    }
  }

  void commit() override {
    state.commit(); cnt.commit(); sector.commit(); dir.commit(); angle.commit();
    passMin.commit(); sonarReq.commit(); sectorReady.commit();
    outSector.commit(); outCm.commit(); sweepDone.commit(); sweepMin.commit();
  }

  static constexpr uint16_t HOME_TICKS     = MS_TO_TICKS(300);
  static constexpr uint16_t WATCHDOG_TICKS = MS_TO_TICKS(MEASURE_TIMEOUT_MS * 2);

private:
  uint16_t settleTicks() const {
    switch (in.mode) {
      case RadarMode::SWEEP_S1: return MS_TO_TICKS(SETTLE_MS_S1);
      default:                  return MS_TO_TICKS(SETTLE_MS_S2);   // S2 and manual
    }
  }
  void park() {
    state.d = S::PARKED; cnt.d = 0; angle.d = ANGLE_MIN;
    sector.d = 0; dir.d = 1; passMin.d = 0;
  }
  void goHome() {
    state.d = S::HOME; cnt.d = 0; angle.d = ANGLE_MIN;
    sector.d = 0; dir.d = 1; passMin.d = 0;
  }
};
