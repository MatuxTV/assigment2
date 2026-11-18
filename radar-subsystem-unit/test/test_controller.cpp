// Host-side testbench (the "VHDL testbench"). Build & run:
//   g++ -std=c++17 -Wall -I../include test_controller.cpp -o /tmp/tb && /tmp/tb
#include <cstdio>
#include <cstdlib>
#include "../include/ControllerFsm.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

// one clock edge; `in` pulses are consumed (cleared) like a debounced strobe
static void clk(ControllerFsm& f, int n = 1) {
  for (int i = 0; i < n; ++i) {
    f.comb(); f.commit();
    f.in = ControllerFsm::In{};
  }
}

int main() {
  ControllerFsm f;

  // idle -> working on START, radar reset pulse appears
  CHECK(f.state.q == RsuState::IDLE);
  f.in.start = true; clk(f);
  CHECK(f.state.q == RsuState::WORKING);
  CHECK(f.stateChanged.q && f.radarReset.q);
  CHECK(f.onLed() && f.radarMode() == RadarMode::SWEEP_S1);
  clk(f);
  CHECK(!f.stateChanged.q && !f.radarReset.q);          // pulses last one cycle

  // object at 60 cm -> pre-alarm, faster sweep, LED on
  f.in.meas = true; f.in.measCm = 60; clk(f);
  CHECK(f.state.q == RsuState::PREALARM);
  CHECK(f.alarmLed() == AlarmLed::ON && f.radarMode() == RadarMode::SWEEP_S2);

  // sweep finished but object still < D2 -> stay
  f.in.sweepDone = true; f.in.sweepMinCm = 60; clk(f);
  CHECK(f.state.q == RsuState::PREALARM);
  // sweep finished and nothing < D2 -> back to working
  f.in.sweepDone = true; f.in.sweepMinCm = 0; clk(f);
  CHECK(f.state.q == RsuState::WORKING);

  // object at 10 cm -> alarm; pot control; blink
  f.in.meas = true; f.in.measCm = 10; clk(f);
  CHECK(f.state.q == RsuState::ALARM);
  CHECK(f.alarmLed() == AlarmLed::BLINK && f.radarMode() == RadarMode::MANUAL_POT);
  // new measurements cannot leave alarm; only RESET can
  f.in.sweepDone = true; clk(f);
  CHECK(f.state.q == RsuState::ALARM);
  f.in.reset = true; clk(f);
  CHECK(f.state.q == RsuState::WORKING && f.radarReset.q);

  // remote control
  f.in.remoteTake = true; clk(f);
  CHECK(f.remote.q && f.radarMode() == RadarMode::MANUAL_REMOTE);
  f.in.remoteRelease = true; clk(f);
  CHECK(!f.remote.q && f.radarMode() == RadarMode::SWEEP_S1);

  // pre-alarm -> alarm directly
  f.in.meas = true; f.in.measCm = 50; clk(f);
  f.in.meas = true; f.in.measCm = 20; clk(f);
  CHECK(f.state.q == RsuState::ALARM);

  // stop -> idle (everything off), then sleep after timeout, wake on start
  f.in.stop = true; clk(f);
  CHECK(f.state.q == RsuState::IDLE && !f.onLed() && f.radarMode() == RadarMode::OFF);
  clk(f, ControllerFsm::IDLE_TO_SLEEP_TICKS - 1);
  CHECK(f.state.q == RsuState::IDLE);
  clk(f, 2);
  CHECK(f.state.q == RsuState::SLEEP && f.deepSleep());
  f.in.start = true; clk(f);
  CHECK(f.state.q == RsuState::WORKING);

  std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
  return fails ? 1 : 0;
}
