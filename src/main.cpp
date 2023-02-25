#include <Arduino.h>

#include "ve_direct.hpp"
#include "wiring_private.h"

const int sc1_tx = 10;
const int sc1_rx = 12;
const int sc2_tx = 2;
const int sc2_rx = 5;
const int sc3_tx = 20;
const int sc3_rx = 21;
const int sc4_tx = 15;
const int sc4_rx = 16;

const int drive1 = 8;
const int drive2 = 9;
const int driveI1 = 14;  // A0
const int driveI2 = 17;  // A3

const int LED_VE1 = 18;  // A4
const int LED_VE2 = 11;
const int LED_VE3 = 13;
const int LED_VE4 = 6;
const int LED_BUS = 7;

const int DE = 3;

/*
Table from Datasheet page 21-23
  Arduino Pin - SAM Pin - SERCOM PAD - SERCOM ALT PAD
* D1/TX0          PA10         0-2          2-2
* D0/RX1          PA11         0-3          2-3

  D10/SS          PA18         1-2          3-2
  D12/MISO        PA19         1-3          3-3

  D2              PA14         2-2          4-2
  D5              PA15         2-3          4-3

  20 (SDA)        PA22         3-0          5-0
  21 (SCL)        PA23         3-1          5-1

  15 (A1)         PB8                       4-0
  16 (A2)         PB9                       4-1

SERCOM 0 = 485 bus
SERCON 1-4 = VE.direct 1-4
*/

Uart Serial(&sercom0, 0, 1, SERCOM_RX_PAD_3, UART_TX_PAD_2);

// messed up physical layout.  this reorganizses the pins
Uart ve1(&sercom2, sc2_rx, sc2_tx, SERCOM_RX_PAD_3, UART_TX_PAD_2);
Uart ve2(&sercom3, sc3_rx, sc3_tx, SERCOM_RX_PAD_1, UART_TX_PAD_0);
Uart ve3(&sercom1, sc1_rx, sc1_tx, SERCOM_RX_PAD_3, UART_TX_PAD_2);
Uart ve4(&sercom4, sc4_rx, sc4_tx, SERCOM_RX_PAD_1, UART_TX_PAD_0);  // works

void SERCOM1_Handler() { ve3.IrqHandler(); }

void SERCOM2_Handler() { ve1.IrqHandler(); }

void SERCOM3_Handler() { ve2.IrqHandler(); }

void SERCOM4_Handler() { ve4.IrqHandler(); }

void SERCOM0_Handler() { Serial.IrqHandler(); }

void pinFlipper(int p) {
  pinMode(p, OUTPUT);
  for (int x = 0; x < 50; x++) {
    digitalWrite(p, HIGH);
    delay(1);
    digitalWrite(p, LOW);
    delay(1);
  }
}

ve_direct ve_direct1(ve1, LED_VE1);
ve_direct ve_direct2(ve2, LED_VE2);
ve_direct ve_direct3(ve3, LED_VE3);
ve_direct ve_direct4(ve4, LED_VE4);

void setup() {
  SerialUSB.begin(921600);
  Serial.begin(38400);
  SerialUSB.println("Hello_World\n\n\n\n\n");

  ve1.begin(19200);
  ve2.begin(19200);
  ve3.begin(19200);
  ve4.begin(19200);

  pinPeripheral(sc1_tx, PIO_SERCOM);
  pinPeripheral(sc1_rx, PIO_SERCOM);
  pinPeripheral(sc2_tx, PIO_SERCOM);
  pinPeripheral(sc2_rx, PIO_SERCOM);
  pinPeripheral(sc3_tx, PIO_SERCOM);
  pinPeripheral(sc3_rx, PIO_SERCOM);
  pinPeripheral(sc4_tx, PIO_SERCOM_ALT);
  pinPeripheral(sc4_rx, PIO_SERCOM_ALT);

  pinMode(drive1, OUTPUT);
  pinMode(drive2, OUTPUT);
  pinMode(driveI1, INPUT);
  pinMode(driveI2, INPUT);
  pinMode(LED_BUS, OUTPUT);
  pinMode(LED_VE1, OUTPUT);
  pinMode(LED_VE2, OUTPUT);
  pinMode(LED_VE3, OUTPUT);
  pinMode(LED_VE4, OUTPUT);
  pinMode(DE, OUTPUT);
  SerialUSB.println("Setup Complete");
}

void rs485_print(String s) {
  digitalWrite(DE, HIGH);
  Serial.println(s);
  Serial.flush();
  digitalWrite(DE, LOW);
}

void loop() {
  ve_direct1.update();
  ve_direct2.update();
  ve_direct3.update();
  ve_direct4.update();

  if (Serial.available()) {
    digitalWrite(LED_BUS, HIGH);
    char c = Serial.read();

    switch (c) {
      case 'a':
        digitalWrite(drive1, LOW);
        SerialUSB.println("Drive A OFF");
        break;
      case 'A':
        digitalWrite(drive1, HIGH);
        SerialUSB.println("Drive A ON");
        break;
      case 'b':
        digitalWrite(drive2, LOW);
        SerialUSB.println("Drive B OFF");
        break;
      case 'B':
        digitalWrite(drive2, HIGH);
        SerialUSB.println("Drive B ON");
        break;
      case 'v': {
        SerialUSB.println("Data Update");
        String data_string = "{\"P1\":";
        ve_direct1.getData(data_string);
        data_string += ",\"P2\":";
        ve_direct2.getData(data_string);
        data_string += ",\"P3\":";
        ve_direct3.getData(data_string);
        data_string += ",\"P4\":";
        ve_direct4.getData(data_string);
        data_string += "}";
        rs485_print(data_string);
      } break;
      default:
        break;
    }
    digitalWrite(LED_BUS, LOW);
  }
}