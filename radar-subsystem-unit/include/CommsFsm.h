#pragma once
#include "Config.h"
#include "Rtl.h"
#include "CommandParser.h"
#include "ControllerFsm.h"

// ============================================================
//  Serial communication FSM (both directions).
//
//  RX: in.cmd (one decoded command per tick, from CommandParser)
//      -> one-cycle pulses startCmd/stopCmd/resetCmd/takeCmd/
//         releaseCmd and the register remoteAngle.
//  TX: events from the other FSMs are queued; at most ONE message
//      leaves per clock, so a slow serial port can never block the
//      system. When the queue is full new messages are dropped
//      (sector data is refreshed on the next pass anyway).
//
//      txValid pulse + txType/txA/txB/txC hold the message that the
//      main.cpp prints with the proto::send*() helpers.
//
//  The ring buffer is RAM: a slot is written in comb() but only
//  becomes visible when `count` is committed.
// ============================================================
class CommsFsm : public Fsm {
public:
  enum MsgType : uint8_t { M_STATE, M_SEC, M_TEMP, M_HB };

  struct In {
    Cmd      cmd = Cmd::NONE;
    uint8_t  cmdAngle = 0;

    bool     stateChanged = false;
    RsuState state = RsuState::IDLE;
    bool     remote = false;

    bool     sectorReady = false;
    uint8_t  sector = 0;
    uint8_t  angle = 0;
    uint16_t cm = 0;

    bool     tempUpdated = false;
    int16_t  tempTenths = 0;
  } in;

  // ---- RX side outputs ----
  Pulse        startCmd, stopCmd, resetCmd, takeCmd, releaseCmd;
  Reg<uint8_t> remoteAngle{90};

  // ---- TX side outputs ----
  Pulse         txValid;
  Reg<uint8_t>  txType{0};
  Reg<uint8_t>  txA{0};
  Reg<uint8_t>  txB{0};
  Reg<uint16_t> txC{0};

  bool idle() const { return count_.q == 0 && !txValid.q; }

  void comb() override {
    // ---- RX ----
    switch (in.cmd) {
      case Cmd::START:   startCmd.d = true;   break;
      case Cmd::STOP:    stopCmd.d = true;    break;
      case Cmd::RESET:   resetCmd.d = true;   break;
      case Cmd::TAKE:    takeCmd.d = true;    break;
      case Cmd::RELEASE: releaseCmd.d = true; break;
      case Cmd::ANGLE:   remoteAngle.d = in.cmdAngle; break;
      default: break;
    }

    // ---- TX: enqueue ----
    hb_.comb();
    uint8_t n = count_.q;
    uint8_t tail = (uint8_t)((head_.q + count_.q) % DEPTH);
    if (in.stateChanged)  push(tail, n, M_STATE, (uint8_t)in.state, 0, 0);
    if (in.sectorReady)   push(tail, n, M_SEC, in.sector, in.angle, in.cm);
    if (in.tempUpdated)   push(tail, n, M_TEMP, 0, 0, (uint16_t)in.tempTenths);
    if (hb_.ce())         push(tail, n, M_HB, (uint8_t)in.state, in.remote ? 1 : 0, 0);

    // ---- TX: dequeue one message per clock ----
    if (count_.q > 0) {
      const Msg& m = q_[head_.q];
      txType.d = m.type; txA.d = m.a; txB.d = m.b; txC.d = m.c;
      txValid.d = true;
      head_.d = (uint8_t)((head_.q + 1) % DEPTH);
      n--;                                   // note: n already contains this tick's pushes
    }
    count_.d = n;
  }

  void commit() override {
    startCmd.commit(); stopCmd.commit(); resetCmd.commit();
    takeCmd.commit();  releaseCmd.commit(); remoteAngle.commit();
    txValid.commit(); txType.commit(); txA.commit(); txB.commit(); txC.commit();
    head_.commit(); count_.commit(); hb_.commit();
  }

  static constexpr uint8_t DEPTH = 8;

private:
  struct Msg { uint8_t type, a, b; uint16_t c; };

  void push(uint8_t& tail, uint8_t& n, uint8_t type, uint8_t a, uint8_t b, uint16_t c) {
    if (n >= DEPTH) return;                  // full -> drop
    q_[tail] = Msg{type, a, b, c};
    tail = (uint8_t)((tail + 1) % DEPTH);
    n++;
  }

  Msg           q_[DEPTH];
  Reg<uint8_t>  head_{0};
  Reg<uint8_t>  count_{0};
  Strobe        hb_{MS_TO_TICKS(1000)};
};
