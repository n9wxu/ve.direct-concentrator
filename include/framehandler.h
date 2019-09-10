//
//  framehandler.h
//  ve_direct_decode
//
//  Created by Joe Julicher on 9/3/19.
//  Copyright © 2019 Joe Julicher. All rights reserved.
//

#ifndef framehandler_h
#define framehandler_h
#include <stdint.h>

class VeDirectFrameHandler
{
private:
    uint8_t mName[9];
    uint8_t mValue[33];
    enum {IDLE,RECORD_BEGIN,RECORD_HEX,RECORD_NAME,RECORD_VALUE,CHECKSUM} mState;
    int8_t mChecksum;
    uint8_t *mTextPointer;
    uint8_t *mValuePointer;
public:
    VeDirectFrameHandler();
    void rxData(uint8_t);
    
    bool hexRxEvent(uint8_t);
    void frameEndEvent(bool);
    
    void logE(const char *, const char *);
    void textRxEvent(uint8_t *, uint8_t *);
};

#endif /* framehandler_h */
