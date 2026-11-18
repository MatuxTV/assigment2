#pragma once
#include <stdint.h>

// ============================================================
//  Smart Radar System - RSU configuration
//
//  This is the ONLY file you need to touch to adapt the
//  firmware to a different wiring or to tune the behaviour.
// ============================================================

// ---------- buttons (INPUT_PULLUP, active LOW) --------------
#define PIN_BTN_START   2   // INT0 - the only pin that wakes the MCU
#define PIN_BTN_STOP    3
#define PIN_BTN_RESET   4

// ---------- actuators ---------------------------------------
#define PIN_LED_ON      11  // green
#define PIN_LED_ALARM   12  // red
#define PIN_SERVO       10  // Timer1 PWM (Servo library)

// ---------- HC-SR04 -----------------------------------------
#define PIN_SONAR_TRIG  9
#define PIN_SONAR_ECHO  8   // PCINT0 - echo width measured by pin-change ISR (main.cpp)

// ---------- analog ------------------------------------------
#define PIN_POT         A0
#define PIN_TEMP        A1

// ---------- LCD ---------------------------------------------
#define LCD_I2C_ADDR    0x27
#define LCD_COLS        16
#define LCD_ROWS        2

// ---------- temperature sensor ------------------------------
// 1 = TMP36 (10 mV/C, 500 mV offset)
// 0 = LM35  (10 mV/C,   0 mV offset)
#define TEMP_IS_TMP36   0

// ---------- radar geometry ----------------------------------
#define SECTOR_COUNT    12
#define ANGLE_MIN       0
#define ANGLE_MAX       180

// ---------- detection thresholds (cm) -----------------------
#define D1_CM           30
#define D2_CM           80
#define SONAR_MAX_CM    200

// ---------- sweep speed (servo settle time per sector, ms) --
#define SETTLE_MS_S1    180
#define SETTLE_MS_S2    90

// ---------- the system clock --------------------------------
//  ONE clock for every FSM (like a VHDL `clk`). Slower activity
//  is a clock-enable strobe: a counter that pulses every N ticks.
#define CLK_MS          5
#define MS_TO_TICKS(ms) ((uint16_t)((ms) / CLK_MS))

// ---------- behaviour ---------------------------------------
#define DEBOUNCE_MS       25
#define IDLE_TO_SLEEP_MS  10000
#define ALARM_BLINK_MS    125     // 4 Hz
#define SONAR_TIMEOUT_US  25000   // ~4.3 m round trip
#define MEASURE_TIMEOUT_MS 120

// ---------- build switches ----------------------------------
#define ENABLE_DEEP_SLEEP 1
#define SERIAL_BAUD       115200

// ------------------------------------------------------------
//  Sector <-> angle helpers
//
//  Sector i (0..N-1) is sampled at the CENTRE of its slice:
//      angle = (i * 180 + 90) / N
//  so with N = 12 the samples are 7, 22, 37 ... 172 degrees.
// ------------------------------------------------------------
static inline uint8_t angleForSector(uint8_t i) {
  return (uint8_t)(((uint16_t)i * 180u + 90u) / SECTOR_COUNT);
}

static inline uint8_t sectorForAngle(uint8_t deg) {
  uint16_t s = ((uint16_t)deg * SECTOR_COUNT) / 180u;
  if (s >= SECTOR_COUNT) s = SECTOR_COUNT - 1;
  return (uint8_t)s;
}
