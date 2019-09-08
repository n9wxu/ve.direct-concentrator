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

}
