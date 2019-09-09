#include <Arduino.h>
#include "wiring_private.h"
#include "ve_direct.hpp"

#define LED 13

/*
Table from Datasheet page 21-23
  Arduino Pin - SAM Pin - SERCOM PAD - SERCOM ALT PAD
  Crystal         PA0                       1-0
  Crystal         PA1                       1-1
  A1              PB8                       4-0
  A2              PB9                       4-1
  A3              PA4                       0-0
  A4              PA5                       0-1
  D8              PA6                       0-2
  D9              PA7                       0-3
  D4              PA8          0-0          2-0
  D3              PA9          0-1          2-1
* D1/TX0          PA10         0-2          2-2
* D0/RX1          PA11         0-3          2-3
  MOSI            PB10                      4-2
  SCK             PB11                      4-3
  --              PB12         4-0
  --              PB13         4-1
  --              PB14         4-2
  --              PB15         4-3
  --              PA12         2-0          4-0
  --              PA13         2-1          4-1
  D2              PA14         2-2          4-2
  D5              PA15         2-3          4-3
  D11/MOSI        PA16         1-0          3-0
  D13/SCK         PA17         1-1          3-1
  D10/SS          PA18         1-2          3-2
  D12/MISO        PA19         1-3          3-3
  --              PB16         5-0
  --              PB17         5-1
  D6              PA20         5-2          3-2
  D7              PA21         5-3          3-3
  SDA             PA22         3-0          5-0
  SCL             PA23         3-1          5-1
  USB D+          PA24         3-2          5-2
  USB D-          PA25         3-3          5-3
  --              PB22                      5-2
  --              PB23                      5-3
  SWCLK           PA30                      1-2
  SWDIO           PA31                      2-3
  --              PB30                      5-0
  --              PB31                      5-1
  --              PB0                       5-2
  --              PB1                       5-3
  A5              PB2                       5-0
  RX_LED          PB3                       5-1
*/

/* PCB VE_Direct Allocation
Channel : TX     : RX
   0      D1 /2    D0 /3
   1      D10/2    D12/3
   2      D2 /2    D5 /3
   3      SDA/0    SCL/1
   4
   5      D6 /2    D7 /3
 */

Uart ve1(&sercom0,  0,  1,SERCOM_RX_PAD_3,UART_TX_PAD_2); // check
Uart ve2(&sercom1, 12, 10,SERCOM_RX_PAD_3,UART_TX_PAD_2); // check
Uart ve3(&sercom2, 5,   4,SERCOM_RX_PAD_3,UART_TX_PAD_2); 
Uart ve4(&sercom3,SCL,SDA,SERCOM_RX_PAD_1,UART_TX_PAD_0); // check

Uart Serial(&sercom5,7,6,SERCOM_RX_PAD_3,UART_TX_PAD_2);

void SERCOM0_Handler()
{
  ve1.IrqHandler();
}

void SERCOM1_Handler()
{
  ve2.IrqHandler();
}

void SERCOM2_Handler()
{
  ve3.IrqHandler();
}

void SERCOM3_Handler()
{
  ve4.IrqHandler();
}

void SERCOM5_Handler()
{
  Serial.IrqHandler();
}

void pinFlipper(int p)
{
  for(int x = 0; x < p; x ++)
  {
    digitalWrite(p,HIGH);
    digitalWrite(p,LOW);
  }
}

ve_direct ve_direct1(ve1);
ve_direct ve_direct2(ve2);
ve_direct ve_direct3(ve3);
ve_direct ve_direct4(ve4);

void setup() 
{
  SerialUSB.begin(115200);
  Serial.begin(115200);

  pinPeripheral(0,PIO_SERCOM);
  pinPeripheral(1,PIO_SERCOM);

  pinPeripheral(12,PIO_SERCOM);
  pinPeripheral(10,PIO_SERCOM);

  pinPeripheral(4,PIO_SERCOM);
  pinPeripheral(5,PIO_SERCOM);

  pinPeripheral(SCL,PIO_SERCOM);
  pinPeripheral(SDA,PIO_SERCOM);

  pinPeripheral(6,PIO_SERCOM);
  pinPeripheral(7,PIO_SERCOM);

}

void sendData(String s1, String s2, String s3, String s4)
{
  if(s1.length || s2.length || s3.length || s4.length)
  {
    bool leadingData = false;
    String outString = "{\"data\":[";

    auto outputString = [&](String s){
      if(s.length)
      {
        if(leadingData) outString += ",";
        leadingData =  true;
        outString += s2;
      }
    };

    outputString(s1);
    outputString(s2);
    outputString(s3);
    outputString(s4);
    outString += "]}";
    Serial.print(outString);
    SerialUSB.println(outString);
  }
}

void loop() {

  String tx1_string = "";
  String tx2_string = "";
  String tx3_string = "";
  String tx4_string = "";

  ve_direct1.update(tx1_string);
  ve_direct2.update(tx2_string);
  ve_direct3.update(tx3_string);
  ve_direct4.update(tx4_string);

  sendData(tx1_string,tx2_string,tx3_string,tx4_string);
}