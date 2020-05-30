//
//  decode.cpp
//  ve_direct_decode
//
//  Created by Joe Julicher on 7/25/19.
//  Copyright © 2019 Joe Julicher. All rights reserved.
//

#include "decode.hpp"

#include "Arduino.h"

#define MODULE "VE.Frame"

VeDirectFrameHandler::VeDirectFrameHandler() : mState(states::WAIT_HEADER), mChecksum(0) { data.clear(); }

void VeDirectFrameHandler::rxData(const uint8_t inbyte) {
  states nextState;

  if (inbyte == hexmarker && mState != states::IN_CHECKSUM) {
    mState = states::IN_HEX;
  }

  mChecksum += inbyte;
  nextState = mState;

  switch (mState) {
    case states::WAIT_HEADER:
      if (inbyte == header2) {
        nextState = states::IN_DATA;
        data.clear();
      }
      break;
    case states::IN_DATA:
      if (inbyte == header1) {
        nextState = states::WAIT_HEADER;
        if (workingString.length() == 0) {
          workingString = "{";
        } else {
          workingString += ",";
        }
        workingString += data.getOutput();
      } else {
        data.addByte(inbyte);
        if (data.isChecksum()) {
          nextState = states::IN_CHECKSUM;
        }
      }
      break;
    case states::IN_CHECKSUM: {
      nextState = states::WAIT_HEADER;
      if (mChecksum % 256 == 0) {
        workingString += "}";
        outputString = workingString;
        dataReady = true;
      } else {
        dataReady = false;
      }
      workingString = "";
      mChecksum = 0;
      break;
    }
    case states::IN_HEX:
      mChecksum = 0;
      if (inbyte == header2) {
        workingString = "";
        dataReady = false;
        nextState = states::WAIT_HEADER;
      }
      break;
    default:
      nextState = states::WAIT_HEADER;
      workingString = "";
      dataReady = false;
      break;
  }
  mState = nextState;
}

bool VeDirectFrameHandler::hexRxEvent(uint8_t b) { return false; }
