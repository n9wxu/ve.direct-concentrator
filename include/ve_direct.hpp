#include <Arduino.h>
#include "wiring_private.h"
#pragma once

class ve_direct
{
    private:
    Uart thisUart;
    public:
    ve_direct(Uart &u);
    ~ve_direct();
    void update(String &outString);
};
