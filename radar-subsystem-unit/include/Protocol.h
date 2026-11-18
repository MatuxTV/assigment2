#pragma once
#include <Arduino.h>
#include <stdint.h>

// ------------------------------------------------------------
//  Wire protocol (ASCII, one message per line, '\n').
//
//  RSU -> CSU
//    STATE,<id>,<NAME>          e.g. STATE,2,PREALARM
//    SEC,<sector>,<angle>,<cm>,<detected>
//    TEMP,<tenths_of_celsius>   e.g. TEMP,235
//    HB,<stateId>,<remote>      heartbeat, 1 Hz
//
//  CSU -> RSU
//    START / STOP / RESET
//    TAKE / RELEASE             optional remote control
//    ANGLE,<0..180>             optional manual position
//
//  Text was chosen on purpose: it is readable in the serial
//  monitor, needs no escaping and TypeScript parses it with
//  split(','). A binary protocol buys nothing at 115200 baud.
// ------------------------------------------------------------
namespace proto {

// state ids - must match State::id()
enum StateId : uint8_t {
  ST_IDLE     = 0,
  ST_WORKING  = 1,
  ST_PREALARM = 2,
  ST_ALARM    = 3,
  ST_SLEEP    = 4,
};

inline void sendState(uint8_t id, const char* name) {
  Serial.print(F("STATE,"));
  Serial.print(id);
  Serial.print(',');
  Serial.println(name);
}

inline void sendSector(uint8_t sector, uint8_t angle, uint16_t cm, bool det) {
  Serial.print(F("SEC,"));
  Serial.print(sector);
  Serial.print(',');
  Serial.print(angle);
  Serial.print(',');
  Serial.print(cm);
  Serial.print(',');
  Serial.println(det ? 1 : 0);
}

inline void sendTemp(int16_t tenths) {
  Serial.print(F("TEMP,"));
  Serial.println(tenths);
}

inline void sendHeartbeat(uint8_t stateId, bool remote) {
  Serial.print(F("HB,"));
  Serial.print(stateId);
  Serial.print(',');
  Serial.println(remote ? 1 : 0);
}

}  // namespace proto
