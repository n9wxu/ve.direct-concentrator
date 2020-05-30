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
  const char cksm[9] = {'C', 'h', 'e', 'c', 'k', 's', 'u', 'm'};
  char key[32];
  char value[32];
  size_t keyIdx;
  size_t valueIdx;
  enum { text, hex, dec } valueType;
  bool keyMode;
  bool cksmKey;
  char outBuffer[80];

 public:
  record() : keyIdx(0), valueIdx(0), keyMode(true), cksmKey(false){};
  void clear() {
    keyIdx = 0;
    valueIdx = 0;
    cksmKey = false;
    keyMode = true;
    valueType = dec;
  };
  void addByte(char b) {
    if (keyMode) {
      if (b == '\t')
        keyMode = false;
      else {
        if (keyIdx < sizeof(cksm)) {
          if (cksm[keyIdx] == b) {
            cksmKey = true;
          }
        } else {
          cksmKey = false;
        }

        key[keyIdx++] = b;
        key[keyIdx] = 0;
      }
    } else {
      bool h = isHexadecimalDigit(b);
      bool d = isDigit(b);
      if (valueType == dec && !d && h) {
        valueType = hex;
      } else if (valueType == hex && !h) {
        valueType = text;
      }

      value[valueIdx++] = b;
      value[valueIdx] = 0;
    }
  }

  bool isChecksum() {
    bool r = cksmKey && !keyMode && keyIdx == (sizeof(cksm) - 1);
    return r;
  };

  char *getOutput() {
    if (valueType == dec) {
      sprintf(outBuffer, "\"%s\":%d", key, atoi(value));
    } else if (valueType == hex) {
      sprintf(outBuffer, "\"%s\":\"%s\"", key, value);
    } else {
      sprintf(outBuffer, "\"%s\":\"%s\"", key, value);
    }
    return outBuffer;
  }
};

class VeDirectFrameHandler {
 private:
  bool dataReady;
  String outputString;
  String workingString;

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
