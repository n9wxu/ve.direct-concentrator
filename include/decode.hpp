//
//  decode.hpp
//  ve_direct_decode
//
//  Created by Joe Julicher on 7/25/19.
//  Copyright © 2019 Joe Julicher. All rights reserved.
//

#ifndef decode_hpp
#define decode_hpp

#include <Arduino.h>

class record {
 private:
  char key[32];
  char value[32];
  int keyIdx;
  int valueIdx;
  bool keyMode;
  char outBuffer[80];

 public:
  record() : keyIdx(0), valueIdx(0), keyMode(true){};
  void clear() {
    keyIdx = 0;
    valueIdx = 0;
  };
  void addByte(char b) {
    if (keyMode) {
      if (b == 0x09)
        keyMode = false;
      else {
        key[keyIdx++] = b;
        key[keyIdx] = 0;
      }
    } else {
      value[valueIdx++] = b;
      value[valueIdx] = 0;
    }
  }

  bool isChecksum() { return strcmp(key, "Checksum") == 0; };

  char *getOutput() {
    sprintf(outBuffer, "{\"%s\":\"%s\"", key, value);
    return outBuffer;
  }
};

class VeDirectFrameHandler {
 private:
  bool dataReady;
  String outputString;

  enum class states { IN_HEX, WAIT_HEADER, IN_DATA, IN_CHECKSUM };
  states mState;
  int8_t mChecksum;

  record data;

  const char header1 = 13;
  const char header2 = 10;
  const char hexmarker = ':';
  const char delimiter = 9;

 public:
  VeDirectFrameHandler();
  void rxData(uint8_t);

  bool hexRxEvent(uint8_t);

  bool isDataReady(void) { return dataReady; };
  String &getString(void) {
    dataReady = false;
    return outputString;
  };
};

#endif /* decode_hpp */
