
#include <Arduino.h>
#undef max
#undef min

#include "wiring_private.h"

#include "decode.hpp"

#pragma once

class ve_direct
{
    private:
    Uart thisUart;
    VeDirectFrameHandler ve_frame;
    public:
    ve_direct(Uart &u);
    ~ve_direct();
    void update(String &outString);
};
