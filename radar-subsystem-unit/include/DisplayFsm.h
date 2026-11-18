#pragma once
#include "Config.h"
#include "Rtl.h"
#include "ControllerFsm.h"

// ============================================================
//  LCD FSM (16x2, I2C).
//
//  An I2C LCD is SLOW: redrawing 32 characters in one go blocks
//  for tens of milliseconds. So the FSM writes ONE character per
//  clock tick:
//
//      WAIT --refresh strobe & in.on--> WRITE (idx 0..31) --> WAIT
//
//  At the start of WRITE a frame is composed in `buf_`; then every
//  tick the pair (wrIdx, wrCh) is published with the `wr` pulse and
//  the main.cpp sends it to the LCD.
//
//  Layout:
//      PREALARM   23.5C         <- state, temperature
//      S 5/12  45cm             <- last visited sector, distance
//  in.on is false in IDLE/SLEEP: nothing is written (the main.cpp
//  clears the screen and switches the backlight off).
// ============================================================
class DisplayFsm : public Fsm {
public:
  struct In {
    bool     on = false;
    RsuState state = RsuState::IDLE;
    bool     remote = false;
    int16_t  tempTenths = 0;
    bool     sectorReady = false;
    uint8_t  sector = 0;
    uint16_t cm = 0;
  } in;

  enum class S : uint8_t { WAIT, WRITE };
  Reg<S>        state{S::WAIT};
  Reg<uint8_t>  idx{0};
  Reg<uint8_t>  lastSector{0};
  Reg<uint16_t> lastCm{0};
  Reg<bool>     haveSector{false};

  Pulse        wr;                 // a character is ready
  Reg<uint8_t> wrIdx{0};           // 0..31  (row = idx / 16, col = idx % 16)
  Reg<char>    wrCh{' '};

  void comb() override {
    refresh_.comb();

    if (in.sectorReady) {
      lastSector.d = in.sector; lastCm.d = in.cm; haveSector.d = true;
    }
    if (!in.on) { haveSector.d = false; }

    switch (state.q) {
      case S::WAIT:
        if (in.on && refresh_.ce()) {
          compose();
          state.d = S::WRITE; idx.d = 0;
        }
        break;

      case S::WRITE:
        if (!in.on) { state.d = S::WAIT; break; }          // switched off mid-frame
        wr.d = true;
        wrIdx.d = idx.q;
        wrCh.d  = buf_[idx.q];
        if (idx.q + 1 >= LCD_COLS * LCD_ROWS) state.d = S::WAIT;
        else                                  idx.d = idx.q + 1;
        break;
    }
  }

  void commit() override {
    state.commit(); idx.commit(); lastSector.commit(); lastCm.commit(); haveSector.commit();
    wr.commit(); wrIdx.commit(); wrCh.commit(); refresh_.commit();
  }

  // exposed for the testbench
  const char* frame() const { return buf_; }

private:
  // helpers: write text / numbers into buf_
  void put(uint8_t pos, const char* s) { while (*s && pos < 32) buf_[pos++] = *s++; }
  uint8_t putUInt(uint8_t pos, uint16_t v) {
    char t[5]; uint8_t n = 0;
    do { t[n++] = '0' + (v % 10); v /= 10; } while (v && n < 5);
    while (n) buf_[pos++] = t[--n];
    return pos;
  }

  void compose() {
    for (uint8_t i = 0; i < 32; ++i) buf_[i] = ' ';
    buf_[32] = 0;

    // row 0: state + temperature
    put(0, stateName(in.state));
    if (in.remote) buf_[8] = 'R';
    int16_t t = in.tempTenths;
    uint8_t p = 9;
    if (t < 0) { buf_[p++] = '-'; t = -t; }
    p = putUInt(p, t / 10);
    buf_[p++] = '.';
    buf_[p++] = '0' + (t % 10);
    buf_[p++] = 'C';

    // row 1: sector / distance
    if (haveSector.q) {
      p = 16;
      buf_[p++] = 'S';
      p = putUInt(p, lastSector.q + 1);
      buf_[p++] = '/';
      p = putUInt(p, SECTOR_COUNT);
      p = 16 + 8;
      if (lastCm.q > 0) { p = putUInt(p, lastCm.q); put(p, "cm"); }
      else              { put(p, "clear"); }
    }
  }

  char   buf_[33] = {0};
  Strobe refresh_{MS_TO_TICKS(250)};
};
