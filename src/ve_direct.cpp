#include "ve_direct.hpp"

ve_direct::ve_direct(Uart &u, int led) : thisUart(u), _led(led), ledTimer(0) {}

ve_direct::~ve_direct() { thisUart.end(); }

const int LED_TIME = 100;

void ve_direct::update(String &outString) {
  int now = millis();
  if (now - ledTimer < LED_TIME) {
    digitalWrite(_led, HIGH);
  } else {
    digitalWrite(_led, LOW);
  }

  if (thisUart.available()) {
    ledTimer = now;
    int c = thisUart.read();
    ve_frame.rxData(c);
    digitalWrite(_led, LOW);
  }
  if (ve_frame.isDataReady()) outString = ve_frame.getString().c_str();
}
