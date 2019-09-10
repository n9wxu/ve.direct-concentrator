//
//  decode.cpp
//  ve_direct_decode
//
//  Created by Joe Julicher on 7/25/19.
//  Copyright © 2019 Joe Julicher. All rights reserved.
//


#include "decode.hpp"

#define MODULE "VE.Frame"

VeDirectFrameHandler::VeDirectFrameHandler()
{
}

void VeDirectFrameHandler::rxData(const uint8_t inbyte)
{
    static enum {IDLE,RECORD_BEGIN,RECORD_HEX,RECORD_NAME,RECORD_VALUE,CHECKSUM} mState=IDLE;
    static int8_t mChecksum=0;
    static std::string key = "";
    static std::string value = "";
    const std::string checksumTagName("CHECKSUM");

    if ( (inbyte == ':') && (mState != CHECKSUM) )
    {
        mState = RECORD_HEX;
    }
    if (mState != RECORD_HEX) {
        mChecksum += inbyte;
    }
    
    switch(mState) {
        case IDLE:
            /* wait for \n of the start of an record */
            if(inbyte == '\n')
            {
                mState = RECORD_BEGIN;
                mChecksum = 0;
            }
            break;
        case RECORD_BEGIN:
            key.clear();
            key+=toupper(inbyte);
            mState = RECORD_NAME;
            break;
        case RECORD_NAME:
            // The record name is being received, terminated by a \t
            if(inbyte == '\t') {
                // the Checksum record indicates a EOR
                if (key.compare(checksumTagName)==0)
                {
                    mState = CHECKSUM;
                    break;
                } else {
                    value.clear();
                    mState = RECORD_VALUE;
                }
            } else {
                key+=toupper(inbyte);
            }
            break;
        case RECORD_VALUE:
            // The record value is being received.  The \r indicates a new record.
            switch(inbyte) {
                case '\n':
                    keyMap[key] = value;
                    mState = RECORD_BEGIN;
                    break;
                case '\r': /* Skip */
                    break;
                default:
                    value += toupper(inbyte);
                    break;
            }
            break;
        case CHECKSUM:
        {
            bool valid = mChecksum == 0;
            if (!valid)
                logE(MODULE,"[CHECKSUM] Invalid frame");
            mChecksum = 0;
            mState = IDLE;
            frameEndEvent(valid);
            break;
        }
        case RECORD_HEX:
            if (hexRxEvent(inbyte)) {
                mChecksum = 0;
                mState = IDLE;
            }
            break;
    }
}

void VeDirectFrameHandler::frameEndEvent(bool v)
{
    if(v)
    {
        outputString += "{";
        for(const auto &it : keyMap)
        {
            outputString += "\"" + it.first + "\":\"" + it.second + "\",";
        }
        outputString += "},";
        dataReady = true;
    }
}

bool VeDirectFrameHandler::hexRxEvent(uint8_t b)
{
    return false;
}

void VeDirectFrameHandler::logE(const char *a, const char *b)
{
//    std::cout << a << "::" << b;
}
