#pragma once
#include <stdint.h>

class ControllerTask;

// ------------------------------------------------------------
//  Variant B - OOP State pattern.
//
//  Every state is a class. The ControllerTask holds a pointer
//  to the active one and delegates to it. This is exactly the
//  VHDL "one process per state" style: the transition logic of
//  a state lives in one place and cannot leak elsewhere.
//
//  Contract:
//    * onTick() is the synchronous FSM body - it is called on
//      every control tick and must return immediately.
//    * a state never changes the pointer itself: it calls
//      ControllerTask::changeTo(), which guarantees that
//      onExit()/onEnter() always run in the right order.
// ------------------------------------------------------------
class State {
public:
  virtual const char* name() const = 0;   // for LCD / serial
  virtual uint8_t     id()   const = 0;   // for the protocol

  virtual void onEnter(ControllerTask& c, uint32_t now) { (void)c; (void)now; }
  virtual void onTick (ControllerTask& c, uint32_t now) = 0;
  virtual void onExit (ControllerTask& c) { (void)c; }

  virtual ~State() {}
};
