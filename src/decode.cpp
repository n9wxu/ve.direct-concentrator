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

VeDirectFrameHandler::VeDirectFrameHandler() {
  key.reserve(32);
  value.reserve(32);
}

void VeDirectFrameHandler::rxData(const uint8_t inbyte) {
  const std::string checksumTagName("CHECKSUM");
  states nextState = mState;

  switch (mState) {
    case states::IDLE:
      /* wait for 10 of the start of an record */
      if (inbyte == 13) {
        nextState = states::RECORD_BEGIN;
        mChecksum = 0;
      } else if (inbyte == ':') {
        nextState = states::RECORD_HEX;
      }

      break;
    case states::RECORD_BEGIN:
      key.clear();
      value.clear();
      key += toupper(inbyte);
      nextState = states::RECORD_NAME;
      break;
    case states::RECORD_NAME:
      // The record name is being received, terminated by a \t
      if (inbyte == 9 || inbyte == 32) {
        // the Checksum record indicates a EOR
        if (key.compare(checksumTagName) == 0) {
          nextState = states::CHECKSUM;
        } else {
          nextState = states::RECORD_VALUE;
        }
      } else {
        key += toupper(inbyte);
      }
      break;
    case states::RECORD_VALUE:
      // The record value is being received.  The \r indicates a new record.
      if (inbyte == 13) {
        dataMap.emplace(key, value);
        nextState = states::RECORD_BEGIN;
      } else if (inbyte == 32) {
      } else {
        value += toupper(inbyte);
        break;
      }
      break;
    case states::CHECKSUM: {
      frameEndEvent(mChecksum == 0);
      nextState = states::IDLE;
      break;
    }
    case states::RECORD_HEX:
      if (hexRxEvent(inbyte)) {
        mChecksum = 0;
        nextState = states::IDLE;
      }
      break;
  }
  mState = nextState;
}

void VeDirectFrameHandler::frameEndEvent(bool v) {
  if (v) {
    outputString = "";
    bool first = false;
    for (auto d : dataMap) {
      if (first) outputString += ",";
      outputString += "\"" + d.first + "\":\"" + d.second + "\"";
      first = true;
    }
    dataReady = true;
  }
}

bool VeDirectFrameHandler::hexRxEvent(uint8_t b) { return false; }
