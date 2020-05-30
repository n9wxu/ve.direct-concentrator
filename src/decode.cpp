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
        SerialUSB.println("ID");
      }
      break;
    case states::IN_DATA:
      if (inbyte == header1) {
        SerialUSB.println("WH");
        nextState = states::WAIT_HEADER;
        if (outputString.length() == 0) {
          outputString = "{";
        } else {
          outputString += ",";
        }
        outputString += data.getOutput();
        data.clear();
      } else if (data.isChecksum()) {
        nextState = states::IN_CHECKSUM;
        SerialUSB.println("IC");
      } else {
        data.addByte(inbyte);
      }
      break;
    case states::IN_CHECKSUM: {
      data.clear();
      nextState = states::WAIT_HEADER;
      if (mChecksum % 256 == 0) {
        outputString += "}";
        dataReady = true;
      } else {
        SerialUSB.println(" CKSM Fail");
        outputString = "";
      }
      mChecksum = 0;
      break;
    }
    case states::IN_HEX:
      SerialUSB.println("IH");
      mChecksum = 0;
      if (inbyte == header2) {
        outputString = "";
        data.clear();
        nextState = states::WAIT_HEADER;
        SerialUSB.println("WH");
      }
      break;
    default:
      nextState = states::WAIT_HEADER;
      SerialUSB.println("Decode Default");
      data.clear();
      outputString = "";
      break;
  }
  mState = nextState;
}

bool VeDirectFrameHandler::hexRxEvent(uint8_t b) { return false; }
