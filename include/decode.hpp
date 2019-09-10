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

class VeDirectFrameHandler
{
private:
    std::map<std::string,std::string> keyMap;
    bool dataReady;
    std::string outputString;
public:
    VeDirectFrameHandler();
    void rxData(uint8_t);
    
    bool hexRxEvent(uint8_t);
    void frameEndEvent(bool);

    bool isDataReady(void) { return dataReady; };
    std::string &getString(void) { dataReady = false; return outputString;};
    
    void logE(const char *, const char *);
};

#endif /* decode_hpp */
