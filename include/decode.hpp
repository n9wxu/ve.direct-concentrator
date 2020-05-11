//
//  decode.hpp
//  ve_direct_decode
//
//  Created by Joe Julicher on 7/25/19.
//  Copyright © 2019 Joe Julicher. All rights reserved.
//

#ifndef decode_hpp
#define decode_hpp

#include <map>
#include <string>

class VeDirectFrameHandler {
 private:
  bool dataReady;
  std::string outputString;
  std::map<std::string, std::string> dataMap;
  enum class states { IDLE, RECORD_BEGIN, RECORD_HEX, RECORD_NAME, RECORD_VALUE, CHECKSUM };
  const char *stateStrings[6] = {"IDLE", "R BEGIN", "R HEX", "R NAME", "R VALUE", "R CHECKSUM"};
  states mState = states::IDLE;
  int8_t mChecksum = 0;
  std::string key = "";
  std::string value = "";

 public:
  VeDirectFrameHandler();
  void rxData(uint8_t);

  bool hexRxEvent(uint8_t);
  void frameEndEvent(bool);

  bool isDataReady(void) { return dataReady; };
  std::string &getString(void) {
    dataReady = false;
    return outputString;
  };
};

#endif /* decode_hpp */
