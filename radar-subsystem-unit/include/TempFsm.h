#pragma once
#include "Config.h"
#include "Rtl.h"

// ============================================================
//  Temperature sampler. Every PERIOD (clock-enable strobe) it
//  latches the ADC reading and converts it to 0.1 degC.
//
//  in.adc : raw 10-bit ADC value, kept up to date by the caller
//  tenths : registered result, e.g. 235 = 23.5 degC
//  updated: pulse when `tenths` got a new value
//
//  Assumes the 5 V ADC reference. 10 mV/degC for both sensors,
//  so 1 mV == 0.1 degC.
// ============================================================
class TempFsm : public Fsm {
public:
  struct In { uint16_t adc = 0; } in;

  Reg<int16_t> tenths{200};
  Pulse        updated;

  static int16_t convert(uint16_t adc) {
    int32_t mv = ((int32_t)adc * 5000L) / 1023L;
#if TEMP_IS_TMP36
    mv -= 500;
#endif
    return (int16_t)mv;
  }

  void comb() override {
    strobe_.comb();
    if (strobe_.ce()) { tenths.d = convert(in.adc); updated.d = true; }
  }
  void commit() override { strobe_.commit(); tenths.commit(); updated.commit(); }

private:
  Strobe strobe_{MS_TO_TICKS(500)};
};
