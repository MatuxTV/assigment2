// Testbench for the peripheral FSMs + a full-system simulation.
//   g++ -std=c++17 -Wall -I../include test_system.cpp -o /tmp/tb2 && /tmp/tb2
#include <cstdio>
#include "../include/ButtonFsm.h"
#include "../include/BlinkerFsm.h"
#include "../include/TempFsm.h"
#include "../include/SonarFsm.h"
#include "../include/RadarFsm.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

template <typename F> static void step(F& f, int n = 1) {
  for (int i = 0; i < n; ++i) { f.comb(); f.commit(); }
}

static void testButton() {
  ButtonFsm b;
  b.in.raw = true;  step(b, 2);                       // bounce shorter than 25 ms
  b.in.raw = false; step(b, 2);
  b.in.raw = true;  step(b, 1);
  b.in.raw = false; step(b, 1);
  CHECK(!b.pressed.q && !b.held());
  b.in.raw = true;
  int pulses = 0;
  for (int i = 0; i < 20; ++i) { step(b); pulses += b.pressed.q; }
  CHECK(pulses == 1 && b.held());                      // exactly one pulse while held
  b.in.raw = false; step(b, 20);
  CHECK(!b.held() && b.state.q == ButtonFsm::S::UP);
}

static void testBlinker() {
  BlinkerFsm l;
  l.in.mode = AlarmLed::ON;  step(l); CHECK(l.out.q);
  l.in.mode = AlarmLed::OFF; step(l); CHECK(!l.out.q);
  l.in.mode = AlarmLed::BLINK;
  int toggles = 0; bool prev = l.out.q;
  for (int i = 0; i < BlinkerFsm::HALF_PERIOD_TICKS * 8; ++i) {
    step(l); if (l.out.q != prev) { ++toggles; prev = l.out.q; }
  }
  CHECK(toggles == 8);
}

static void testTemp() {
  CHECK(TempFsm::convert(0) == (TEMP_IS_TMP36 ? -500 : 0));
  TempFsm t;
  int ticks = 0;
  t.in.adc = 51;                                       // ~249 mV -> ~24.9 degC (LM35)
  while (!t.updated.q && ticks++ < 1000) step(t);
  CHECK(t.updated.q && t.tenths.q > 240 && t.tenths.q < 260);
}

static void testSonar() {
  // 20 degC: v = 343.4 m/s -> 1 cm = 58.2 us round trip
  CHECK(SonarFsm::toCm(1164, 200) >= 19 && SonarFsm::toCm(1164, 200) <= 21);
  CHECK(SonarFsm::toCm(0, 200) == 0);
  CHECK(SonarFsm::toCm(30000, 200) == 0);              // beyond SONAR_MAX_CM
  // hotter air -> faster sound -> same echo means a longer distance
  CHECK(SonarFsm::toCm(1164, 400) > SonarFsm::toCm(1164, 0));

  SonarFsm s;
  s.in.req = true; step(s); s.in.req = false;
  CHECK(s.trig() && s.armEcho());
  step(s); CHECK(!s.trig() && s.state.q == SonarFsm::S::WAIT);
  s.in.echoValid = true; s.in.echoUs = 1164; step(s);
  CHECK(s.done.q && s.cm.q >= 19 && s.cm.q <= 21 && !s.busy());
  step(s); CHECK(!s.done.q);

  // timeout: no echo
  s.in.echoValid = false; s.in.req = true; step(s); s.in.req = false;
  int n = 0; while (!s.done.q && n++ < 100) step(s);
  CHECK(s.done.q && s.cm.q == 0 && n <= SonarFsm::TIMEOUT_TICKS + 2);
}

// ------------------------------------------------------------
//  Full system: wiring = VHDL "architecture" (concurrent
//  assignments from the registered outputs of one FSM to the
//  inputs of another). A fake sonar replies after the trigger.
// ------------------------------------------------------------
struct System {
  ButtonFsm     bStart, bStop, bReset;
  ControllerFsm ctrl;
  RadarFsm      radar;
  SonarFsm      sonar;
  BlinkerFsm    alarm;
  TempFsm       temp;

  uint16_t obstacleCm[SECTOR_COUNT];      // what the fake sonar "sees"
  uint8_t  pot = 90;
  uint32_t ticks = 0;
  bool     rawStart = false, rawStop = false, rawReset = false;
  int      sectorEvents = 0, sweeps = 0;
  uint8_t  lastSector = 0;

  System() { for (auto& c : obstacleCm) c = 0; }

  void wire() {
    bStart.in.raw = rawStart; bStop.in.raw = rawStop; bReset.in.raw = rawReset;

    ctrl.in.start = bStart.pressed.q;
    ctrl.in.stop  = bStop.pressed.q;
    ctrl.in.reset = bReset.pressed.q;
    ctrl.in.meas       = radar.sectorReady.q;
    ctrl.in.measCm     = radar.outCm.q;
    ctrl.in.sweepDone  = radar.sweepDone.q;
    ctrl.in.sweepMinCm = radar.sweepMin.q;

    radar.in.mode        = ctrl.radarMode();
    radar.in.reset       = ctrl.radarReset.q;
    radar.in.manualAngle = pot;
    radar.in.sonarDone   = sonar.done.q;
    radar.in.sonarCm     = sonar.cm.q;

    sonar.in.req = radar.sonarReq.q;
    sonar.in.tempTenths = temp.tenths.q;
    alarm.in.mode = ctrl.alarmLed();

    // fake HC-SR04: echo width for the sector the servo points at
    if (sonar.armEcho()) { sonar.in.echoValid = false; }
    if (sonar.state.q == SonarFsm::S::WAIT && !sonar.in.echoValid) {
      uint16_t cm = obstacleCm[sectorForAngle(radar.angle.q)];
      sonar.in.echoUs = cm ? (uint32_t)(cm * 2.0f / soundCmPerUs(temp.tenths.q / 10.0f)) : 0;
      sonar.in.echoValid = cm != 0;       // cm==0 -> no echo, the FSM times out
    }
  }

  void tick() {
    wire();
    Fsm* all[] = { &bStart, &bStop, &bReset, &ctrl, &radar, &sonar, &alarm, &temp };
    for (Fsm* f : all) f->comb();
    for (Fsm* f : all) f->commit();
    ++ticks;
    if (radar.sectorReady.q) { ++sectorEvents; lastSector = radar.outSector.q; }
    if (radar.sweepDone.q) ++sweeps;
  }
  void run(int n) { for (int i = 0; i < n; ++i) tick(); }
  void press(bool& raw) { raw = true; run(30); raw = false; run(30); }
};

static void testSystem() {
  System s;
  s.run(10);
  CHECK(s.ctrl.state.q == RsuState::IDLE && !s.ctrl.onLed());

  // START -> working; radar homes then sweeps all sectors in order
  s.press(s.rawStart);
  CHECK(s.ctrl.state.q == RsuState::WORKING);
  s.run(2000 / CLK_MS * 4);                              // ~8 s: several passes at S1
  CHECK(s.sweeps >= 2);
  CHECK(s.sectorEvents >= 2 * SECTOR_COUNT);
  CHECK(s.ctrl.state.q == RsuState::WORKING);            // nothing in range

  // object at 60 cm in sector 5 -> pre-alarm, ALARM led steady on
  s.obstacleCm[5] = 60;
  int guard = 0;
  while (s.ctrl.state.q != RsuState::PREALARM && guard++ < 4000) s.tick();
  CHECK(s.ctrl.state.q == RsuState::PREALARM);
  s.run(2);
  CHECK(s.radar.in.mode == RadarMode::SWEEP_S2);
  CHECK(s.alarm.out.q);

  // object leaves -> back to working after the next pass
  s.obstacleCm[5] = 0;
  guard = 0;
  while (s.ctrl.state.q != RsuState::WORKING && guard++ < 4000) s.tick();
  CHECK(s.ctrl.state.q == RsuState::WORKING);

  // intruder at 15 cm in sector 8 -> alarm, blinking, pot controls the servo
  s.obstacleCm[8] = 15;
  guard = 0;
  while (s.ctrl.state.q != RsuState::ALARM && guard++ < 4000) s.tick();
  CHECK(s.ctrl.state.q == RsuState::ALARM);
  s.pot = 30; s.run(200);
  CHECK(s.radar.angle.q == 30);
  s.pot = 150; s.run(200);
  CHECK(s.radar.angle.q == 150);
  int toggles = 0; bool prev = s.alarm.out.q;
  for (int i = 0; i < 200; ++i) { s.tick(); if (s.alarm.out.q != prev) { ++toggles; prev = s.alarm.out.q; } }
  CHECK(toggles >= 6);
  // alarm persists even though the pot sees clear air; only RESET leaves it
  s.obstacleCm[8] = 0; s.run(400);
  CHECK(s.ctrl.state.q == RsuState::ALARM);
  s.press(s.rawReset);
  CHECK(s.ctrl.state.q == RsuState::WORKING);
  s.run(600);
  CHECK(s.sweeps >= 3);

  // STOP -> idle, everything off, servo parked
  s.press(s.rawStop);
  s.run(10);
  CHECK(s.ctrl.state.q == RsuState::IDLE && !s.ctrl.onLed());
  CHECK(!s.radar.servoOn() && !s.alarm.out.q);
}

int main() {
  testButton(); testBlinker(); testTemp(); testSonar(); testSystem();
  std::printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
  return fails ? 1 : 0;
}
