#pragma once
#include <stdint.h>
#include <string.h>

// ============================================================
//  Parser for the CSU -> RSU lines. Pure C++ (no Arduino), so
//  it is testable on the PC.
//
//  main.cpp feeds it byte by byte. While a command is pending
//  it refuses more bytes - they simply stay in the serial
//  buffer until the FSM has consumed the command.
// ============================================================
enum class Cmd : uint8_t { NONE, START, STOP, RESET, TAKE, RELEASE, ANGLE };

class CommandParser {
public:
  bool pending() const { return cmd_ != Cmd::NONE; }

  // returns the pending command and clears it
  Cmd take(uint8_t* angle) {
    Cmd c = cmd_;
    if (angle) *angle = angle_;
    cmd_ = Cmd::NONE;
    return c;
  }

  // feed one byte; ignored while a command is waiting
  void feed(char ch) {
    if (pending()) return;
    if (ch == '\r') return;
    if (ch == '\n') { parse(); len_ = 0; overflow_ = false; return; }
    if (len_ < sizeof(buf_) - 1) buf_[len_++] = ch; else overflow_ = true;
  }

private:
  void parse() {
    if (overflow_) return;
    buf_[len_] = 0;
    if      (!strcmp(buf_, "START"))   cmd_ = Cmd::START;
    else if (!strcmp(buf_, "STOP"))    cmd_ = Cmd::STOP;
    else if (!strcmp(buf_, "RESET"))   cmd_ = Cmd::RESET;
    else if (!strcmp(buf_, "TAKE"))    cmd_ = Cmd::TAKE;
    else if (!strcmp(buf_, "RELEASE")) cmd_ = Cmd::RELEASE;
    else if (!strncmp(buf_, "ANGLE,", 6)) {
      const char* p = buf_ + 6;
      if (*p < '0' || *p > '9') return;
      uint16_t v = 0;
      while (*p >= '0' && *p <= '9' && v < 1000) v = v * 10 + (*p++ - '0');
      if (*p != 0) return;                       // trailing garbage
      angle_ = v > 180 ? 180 : (uint8_t)v;
      cmd_ = Cmd::ANGLE;
    }
  }

  char     buf_[12];
  uint8_t  len_ = 0;
  bool     overflow_ = false;
  Cmd      cmd_ = Cmd::NONE;
  uint8_t  angle_ = 0;
};
