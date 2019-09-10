#include "ve_direct.hpp"

ve_direct::ve_direct(Uart &u) : thisUart(u)
{
    thisUart.begin(19200);
}

ve_direct::~ve_direct()
{
    thisUart.end();
}

void ve_direct::update(String &outString)
{
    if(thisUart.available())
    {
        ve_frame.rxData(thisUart.read());
    }
    if(ve_frame.isDataReady())
        outString = ve_frame.getString().c_str();
}
