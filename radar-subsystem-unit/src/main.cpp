// ============================================================
//  Smart Radar System - RSU (Arduino UNO)
//
//  Architecture: synchronous FSMs driven by ONE clock, written in
//  the style of VHDL (see Rtl.h).
//
//    loop():  if the clock edge is due
//               wire()   - concurrent assignments: registered outputs
//                          of one FSM -> inputs of another, plus the
//                          physical inputs (pins, ADC, serial)
//               clk.tick() - comb() of ALL FSMs, then commit() of all
//               drive()  - registered outputs -> pins, servo, LCD, serial
//
//  Libraries: Servo, Wire, LiquidCrystal_I2C (Frank de Brabander).
// ============================================================
#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <avr/sleep.h>

#include "Config.h"
#include "Rtl.h"
#include "ButtonFsm.h"
#include "BlinkerFsm.h"
#include "TempFsm.h"
#include "SonarFsm.h"
#include "RadarFsm.h"
#include "ControllerFsm.h"
#include "CommsFsm.h"
#include "DisplayFsm.h"
#include "CommandParser.h"
#include "Protocol.h"

// ---------- the FSM instances ("entities") --------------------
static ButtonFsm     bStart, bStop, bReset;
static ControllerFsm ctrl;
static RadarFsm      radar;
static SonarFsm      sonar;
static BlinkerFsm    alarmLed;
static TempFsm       temp;
static CommsFsm      comms;
static DisplayFsm    disp;

static Fsm* fsms[] = { &bStart, &bStop, &bReset, &ctrl, &radar, &sonar,
                       &alarmLed, &temp, &comms, &disp };
static Clock clk(fsms, sizeof(fsms) / sizeof(fsms[0]), CLK_MS);

// ---------- hardware ------------------------------------------
static Servo             servo;
static LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLS, LCD_ROWS);
static CommandParser     parser;

// echo width captured by the PCINT0 ISR (the only asynchronous input)
static volatile uint32_t echoRiseUs  = 0;
static volatile uint32_t echoWidthUs = 0;
static volatile bool     echoCaptured = false;

static volatile bool wokeFromSleep = false;
static bool          wakeStart = false;   // wake-up press acts as START
static bool          lcdOn = false;
static uint8_t       lastServoAngle = 255;
static uint8_t       potAngle = 90;

// Pin 8 is not an external-interrupt pin on the UNO (only 2 and 3 are),
// so the echo is captured with a pin-change interrupt (PB0 = PCINT0).
static_assert(PIN_SONAR_ECHO == 8, "echo ISR below is wired to pin 8 (PCINT0)");
ISR(PCINT0_vect) {
  if (digitalRead(PIN_SONAR_ECHO)) {
    echoRiseUs = micros();
  } else {
    echoWidthUs = micros() - echoRiseUs;
    echoCaptured = true;
  }
}
static void wakeIsr() { wokeFromSleep = true; }

// ------------------------------------------------------------
//  wire(): everything that is "a signal between entities"
// ------------------------------------------------------------
static void wire() {
  // --- physical inputs ---
  bStart.in.raw = (digitalRead(PIN_BTN_START) == LOW);
  bStop.in.raw  = (digitalRead(PIN_BTN_STOP)  == LOW);
  bReset.in.raw = (digitalRead(PIN_BTN_RESET) == LOW);

  temp.in.adc = analogRead(PIN_TEMP);

  uint8_t p = (uint8_t)map(analogRead(PIN_POT), 0, 1023, ANGLE_MIN, ANGLE_MAX);
  if (abs((int)p - (int)potAngle) >= 2) potAngle = p;       // hysteresis: no servo jitter

  // --- serial RX: at most one command per clock edge ---
  while (Serial.available() && !parser.pending()) parser.feed((char)Serial.read());
  comms.in.cmd = parser.take(&comms.in.cmdAngle);

  // --- echo capture (shared with the ISR) ---
  if (sonar.armEcho()) echoCaptured = false;                // new measurement: forget old echo
  noInterrupts();
  bool     cap = echoCaptured;
  uint32_t us  = echoWidthUs;
  interrupts();
  sonar.in.echoValid = cap;
  sonar.in.echoUs    = us;
  sonar.in.tempTenths = temp.tenths.q;
  sonar.in.req        = radar.sonarReq.q;

  // --- Controller ---
  ctrl.in.start = bStart.pressed.q || comms.startCmd.q || wakeStart;
  wakeStart = false;
  ctrl.in.stop  = bStop.pressed.q  || comms.stopCmd.q;
  ctrl.in.reset = bReset.pressed.q || comms.resetCmd.q;
  ctrl.in.remoteTake    = comms.takeCmd.q;
  ctrl.in.remoteRelease = comms.releaseCmd.q;
  ctrl.in.meas       = radar.sectorReady.q;
  ctrl.in.measCm     = radar.outCm.q;
  ctrl.in.sweepDone  = radar.sweepDone.q;
  ctrl.in.sweepMinCm = radar.sweepMin.q;

  // --- Radar ---
  radar.in.mode  = ctrl.radarMode();
  radar.in.reset = ctrl.radarReset.q;
  radar.in.manualAngle = (ctrl.radarMode() == RadarMode::MANUAL_REMOTE) ? comms.remoteAngle.q : potAngle;
  radar.in.sonarDone = sonar.done.q;
  radar.in.sonarCm   = sonar.cm.q;

  // --- LED / display / comms ---
  alarmLed.in.mode = ctrl.alarmLed();

  disp.in.on          = ctrl.onLed();
  disp.in.state       = ctrl.state.q;
  disp.in.remote      = ctrl.remote.q;
  disp.in.tempTenths  = temp.tenths.q;
  disp.in.sectorReady = radar.sectorReady.q;
  disp.in.sector      = radar.outSector.q;
  disp.in.cm          = radar.outCm.q;

  comms.in.stateChanged = ctrl.stateChanged.q;
  comms.in.state        = ctrl.state.q;
  comms.in.remote       = ctrl.remote.q;
  comms.in.sectorReady  = radar.sectorReady.q;
  comms.in.sector       = radar.outSector.q;
  comms.in.angle        = radar.angle.q;
  comms.in.cm           = radar.outCm.q;
  comms.in.tempUpdated  = temp.updated.q;
  comms.in.tempTenths   = temp.tenths.q;
}

// ------------------------------------------------------------
//  drive(): registered outputs -> the real world
// ------------------------------------------------------------
static void drive() {
  digitalWrite(PIN_SONAR_TRIG, sonar.trig() ? HIGH : LOW);
  digitalWrite(PIN_LED_ON,     ctrl.onLed() ? HIGH : LOW);
  digitalWrite(PIN_LED_ALARM,  alarmLed.out.q ? HIGH : LOW);

  // servo: powered only while the radar runs
  if (radar.servoOn()) {
    if (!servo.attached()) { servo.attach(PIN_SERVO); lastServoAngle = 255; }
    if (radar.angle.q != lastServoAngle) {
      lastServoAngle = radar.angle.q;
      servo.write(lastServoAngle);
    }
  } else if (servo.attached()) {
    servo.detach();
  }

  // LCD: on/off with the system, characters arrive one per clock
  bool on = ctrl.onLed();
  if (on != lcdOn) {
    lcdOn = on;
    lcd.clear();
    if (on) lcd.backlight(); else lcd.noBacklight();
  }
  if (lcdOn && disp.wr.q) {
    uint8_t i = disp.wrIdx.q;
    if (i % LCD_COLS == 0) lcd.setCursor(0, i / LCD_COLS);
    lcd.write(disp.wrCh.q);
  }

  // serial TX: one message per clock
  if (comms.txValid.q) {
    switch (comms.txType.q) {
      case CommsFsm::M_STATE:
        proto::sendState(comms.txA.q, stateName((RsuState)comms.txA.q));
        break;
      case CommsFsm::M_SEC:
        proto::sendSector(comms.txA.q, comms.txB.q, comms.txC.q, comms.txC.q > 0);
        break;
      case CommsFsm::M_TEMP:
        proto::sendTemp((int16_t)comms.txC.q);
        break;
      case CommsFsm::M_HB:
        proto::sendHeartbeat(comms.txA.q, comms.txB.q != 0);
        break;
    }
  }
}

// ------------------------------------------------------------
//  Deep sleep: only the START button (INT0, level LOW) wakes us.
//  millis() does not run in power-down, so the clock is re-synced.
// ------------------------------------------------------------
static void goToSleep() {
  Serial.flush();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  noInterrupts();
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_START), wakeIsr, LOW);
  wokeFromSleep = false;
  sleep_enable();
  interrupts();
  sleep_cpu();                    // ... sleeping ...
  sleep_disable();
  detachInterrupt(digitalPinToInterrupt(PIN_BTN_START));
  wakeStart = true;               // the wake-up press is the START command
  clk.begin(millis());
}

void setup() {
  pinMode(PIN_BTN_START, INPUT_PULLUP);
  pinMode(PIN_BTN_STOP,  INPUT_PULLUP);
  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  pinMode(PIN_LED_ON,    OUTPUT);
  pinMode(PIN_LED_ALARM, OUTPUT);
  pinMode(PIN_SONAR_TRIG, OUTPUT);
  pinMode(PIN_SONAR_ECHO, INPUT);

  Serial.begin(SERIAL_BAUD);
  lcd.init();
  lcd.noBacklight();
  lcd.clear();

  PCMSK0 |= _BV(PCINT0);          // enable pin-change on D8 only
  PCICR  |= _BV(PCIE0);
  clk.begin(millis());
}

void loop() {
  if (clk.due(millis())) {
    wire();
    clk.tick();
    drive();
  }

#if ENABLE_DEEP_SLEEP
  // sleep only when the SLEEP state has been announced on the serial line
  if (ctrl.deepSleep() && comms.idle() && !ctrl.stateChanged.q && !wakeStart) {
    goToSleep();
  }
#endif
}
