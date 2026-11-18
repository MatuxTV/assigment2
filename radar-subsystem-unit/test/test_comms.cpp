// Testbench: CommandParser, CommsFsm, DisplayFsm.
//   g++ -std=c++17 -Wall -Wextra -I../include test_comms.cpp -o /tmp/tb3 && /tmp/tb3
#include <cstdio>
#include <cstring>
#include "../include/CommsFsm.h"
#include "../include/DisplayFsm.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

template <typename F> static void step(F& f, int n = 1) {
  for (int i = 0; i < n; ++i) { f.comb(); f.commit(); }
}

static void feed(CommandParser& p, const char* s) { while (*s) p.feed(*s++); }

static void testParser() {
  CommandParser p; uint8_t a = 0;
  feed(p, "START\n");        CHECK(p.take(&a) == Cmd::START);
  feed(p, "RESET\r\n");      CHECK(p.take(&a) == Cmd::RESET);
  feed(p, "ANGLE,120\n");    CHECK(p.take(&a) == Cmd::ANGLE && a == 120);
  feed(p, "ANGLE,999\n");    CHECK(p.take(&a) == Cmd::ANGLE && a == 180);   // clamped
  feed(p, "ANGLE,x\n");      CHECK(!p.pending());
  feed(p, "ANGLE,12x\n");    CHECK(!p.pending());
  feed(p, "NONSENSE\n");     CHECK(!p.pending());
  feed(p, "AAAAAAAAAAAAAAAAAAAAAAAA\nTAKE\n"); CHECK(p.take(&a) == Cmd::TAKE);  // overflow line dropped
  // a second command waits in the serial buffer while the first is pending
  feed(p, "STOP\n"); feed(p, "TAKE\n");
  CHECK(p.take(&a) == Cmd::STOP && !p.pending());
}

static void testCommsRx() {
  CommsFsm c;
  c.in.cmd = Cmd::START; step(c); c.in.cmd = Cmd::NONE;
  CHECK(c.startCmd.q); step(c); CHECK(!c.startCmd.q);
  c.in.cmd = Cmd::ANGLE; c.in.cmdAngle = 42; step(c); c.in.cmd = Cmd::NONE;
  CHECK(c.remoteAngle.q == 42);
  step(c, 5); CHECK(c.remoteAngle.q == 42);              // register holds
}

static void testCommsTx() {
  CommsFsm c;
  step(c, 3);                                            // flush the HB sent on the first clock
  // 3 events in one clock -> 3 messages, one per clock, in order
  c.in.stateChanged = true; c.in.state = RsuState::WORKING;
  c.in.sectorReady = true; c.in.sector = 4; c.in.angle = 67; c.in.cm = 55;
  c.in.tempUpdated = true; c.in.tempTenths = 235;
  step(c);
  c.in.stateChanged = c.in.sectorReady = c.in.tempUpdated = false;

  CHECK(!c.txValid.q && !c.idle());
  step(c); CHECK(c.txValid.q && c.txType.q == CommsFsm::M_STATE && c.txA.q == 1);
  step(c); CHECK(c.txValid.q && c.txType.q == CommsFsm::M_SEC && c.txA.q == 4 && c.txB.q == 67 && c.txC.q == 55);
  step(c); CHECK(c.txValid.q && c.txType.q == CommsFsm::M_TEMP && (int16_t)c.txC.q == 235);
  step(c); CHECK(!c.txValid.q && c.idle());

  // heartbeat once per second
  int hb = 0;
  for (int i = 0; i < MS_TO_TICKS(3000) + 5; ++i) { step(c); hb += (c.txValid.q && c.txType.q == CommsFsm::M_HB); }
  CHECK(hb == 3);

  // overflow: more events than the queue holds never corrupts or crashes
  for (int i = 0; i < 40; ++i) { c.in.sectorReady = true; c.in.sector = i % 12; step(c); }
  c.in.sectorReady = false;
  int got = 0; for (int i = 0; i < 40; ++i) { step(c); got += c.txValid.q; }
  CHECK(got > 0 && got <= 40 && c.idle());
}

static void testDisplay() {
  DisplayFsm d;
  d.in.on = false; d.in.state = RsuState::IDLE;
  step(d, 200); CHECK(!d.wr.q && d.state.q == DisplayFsm::S::WAIT);       // off -> silent

  d.in.on = true; d.in.state = RsuState::PREALARM; d.in.tempTenths = 235;
  d.in.sectorReady = true; d.in.sector = 4; d.in.cm = 45; step(d); d.in.sectorReady = false;

  // collect a full frame, one character per tick
  char screen[33]; std::memset(screen, '?', 32); screen[32] = 0;
  int chars = 0, guard = 0;
  while (chars < 64 && guard++ < 800) {
    step(d);
    if (d.wr.q) { screen[d.wrIdx.q] = d.wrCh.q; ++chars; }
  }
  CHECK(chars == 64);
  CHECK(std::strncmp(screen,      "PREALARM 23.5C  ", 16) == 0);
  CHECK(std::strncmp(screen + 16, "S5/12   45cm     ", 16) == 0);

  d.in.sectorReady = true; d.in.sector = 11; d.in.cm = 0; step(d); d.in.sectorReady = false;
  d.in.state = RsuState::WORKING; d.in.remote = true;
  chars = 0; guard = 0;
  while (chars < 64 && guard++ < 800) { step(d); if (d.wr.q) { screen[d.wrIdx.q] = d.wrCh.q; ++chars; } }
  CHECK(std::strncmp(screen,      "WORKING R23.5C  ", 16) == 0);
  CHECK(std::strncmp(screen + 16, "S12/12  clear    ", 16) == 0);

  // switched off mid-frame -> stops writing
  d.in.on = true; int w = 0;
  while (!d.wr.q) step(d);
  d.in.on = false; step(d, 3);
  for (int i = 0; i < 100; ++i) { step(d); w += d.wr.q; }
  CHECK(w == 0);
}

int main() {
  testParser(); testCommsRx(); testCommsTx(); testDisplay();
  std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
  return fails ? 1 : 0;
}
