
#include <Arduino.h>
#undef max
#undef min

#include "decode.hpp"
#include "wiring_private.h"

#pragma once

class ve_direct {
 private:
  Uart &thisUart;
  VeDirectFrameHandler ve_frame;
  int _led;
  int ledTimer;

 public:
  ve_direct(Uart &u, int l);
  ~ve_direct();
  void update(String &outString);
};
