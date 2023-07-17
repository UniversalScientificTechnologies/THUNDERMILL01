// Thundermill for LS

// Compiled with: Arduino 2.1.1
//#define DEBUG

/*
  THUNDERMILL

ISP
---
PD0     RX
PD1     TX
RESET#  through 50M capacitor to RST#

ANALOG
------
+      A0  PA0
-      A1  PA1

LED
---
LED1  13  PD5         
LED2  14  PD6         
LED3  15  PD7         



                     Mighty 1284p    
                      +---\/---+
           (D 0) PB0 1|        |40 PA0 (AI 0 / D24)
           (D 1) PB1 2|        |39 PA1 (AI 1 / D25)
      INT2 (D 2) PB2 3|        |38 PA2 (AI 2 / D26)
       PWM (D 3) PB3 4|        |37 PA3 (AI 3 / D27)
    PWM/SS (D 4) PB4 5|        |36 PA4 (AI 4 / D28)
      MOSI (D 5) PB5 6|        |35 PA5 (AI 5 / D29)
  PWM/MISO (D 6) PB6 7|        |34 PA6 (AI 6 / D30)
   PWM/SCK (D 7) PB7 8|        |33 PA7 (AI 7 / D31)
                 RST 9|        |32 AREF
                VCC 10|        |31 GND
                GND 11|        |30 AVCC
              XTAL2 12|        |29 PC7 (D 23)
              XTAL1 13|        |28 PC6 (D 22)
      RX0 (D 8) PD0 14|        |27 PC5 (D 21) TDI
      TX0 (D 9) PD1 15|        |26 PC4 (D 20) TDO
RX1/INT0 (D 10) PD2 16|        |25 PC3 (D 19) TMS
TX1/INT1 (D 11) PD3 17|        |24 PC2 (D 18) TCK
     PWM (D 12) PD4 18|        |23 PC1 (D 17) SDA
     PWM (D 13) PD5 19|        |22 PC0 (D 16) SCL
     PWM (D 14) PD6 20|        |21 PD7 (D 15) PWM
                      +--------+
*/

#define ADDR_DRV 0b1010010 // motor driver

#define RANGE 200   // size of output buffer

#include "wiring_private.h"
#include <Wire.h> 
          
#define LED1  13      //PD5         
#define LED2  14      //PD6         
#define LED3  15      //PD7         
#define COUNT1  27    //PA3       
#define COUNT2  20    //PC4         
#define TIMEPULSE  12 //PD4         
#define EXTINT  2     //PB2         

// Read Analog Differential without gain (read datashet of ATMega1280 and ATMega2560 for refference)
// Use analogReadDiff(NUM)
//   NUM  | POS PIN             | NEG PIN           |   GAIN
//  0 | A0      | A1      | 1x
//  1 | A1      | A1      | 1x
//  2 | A2      | A1      | 1x
//  3 | A3      | A1      | 1x
//  4 | A4      | A1      | 1x
//  5 | A5      | A1      | 1x
//  6 | A6      | A1      | 1x
//  7 | A7      | A1      | 1x
//  8 | A8      | A9      | 1x
//  9 | A9      | A9      | 1x
//  10  | A10     | A9      | 1x
//  11  | A11     | A9      | 1x
//  12  | A12     | A9      | 1x
//  13  | A13     | A9      | 1x
//  14  | A14     | A9      | 1x
//  15  | A15     | A9      | 1x
#define PIN 0
uint8_t analog_reference = INTERNAL2V56; // DEFAULT, INTERNAL, INTERNAL1V1, INTERNAL2V56, or EXTERNAL

void write_twoByte(int address, unsigned char r, uint16_t data){
  // uint16_t begin = 0;
  // begin = read_twoByte(address, r);
  delay(100);

  Wire.beginTransmission(address);
  Wire.write(r);
  Wire.write((data >> 8));
  Wire.write(data & 0xFF);
  Wire.endTransmission(true);
  
  // delay(100);
  // uint16_t end = 0;
  // end = read_twoByte(address, r);
  // Serial.printf("reg 0x%04X: 0x%04X -> 0x%04X -> 0x%04X \r\n", r, begin, data, end);
}


void setup()
{
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(COUNT1, INPUT);
  pinMode(COUNT2, INPUT);
  pinMode(TIMEPULSE, INPUT);
  pinMode(EXTINT, INPUT);

/*  debug output for oscilloscope
 *   
  pinMode(EXTINT, OUTPUT);
  digitalWrite(EXTINT, LOW);  

  digitalWrite(EXTINT, HIGH);
  digitalWrite(EXTINT, LOW);
*/
  pinMode(24, INPUT);
  pinMode(25, INPUT);

  Wire.begin();        // join i2c bus (address optional for master)
  Wire.setClock(100000);


  for(int i=0; i<3; i++)  
  {
    digitalWrite(LED1, LOW);  
    delay(100);
    digitalWrite(LED1, HIGH);  
    delay(100);
  }
  
  // Open serial communications
  Serial.begin(9600, SERIAL_8N2);
  Serial1.begin(9600, SERIAL_8N2);

  for(int i=0; i<3; i++)  
  {
    digitalWrite(LED2, LOW);  
    delay(100);
    digitalWrite(LED2, HIGH);  
    delay(100);
  }

  //ADMUX = (analog_reference << 6) | ((PIN | 0x10) & 0x1F);
  ADMUX = (analog_reference << 6) | 0x010000; // +AD0 & -AD1, 1x, ADLAR right adjusted
  
  ADCSRB = 1;               // Switching ADC to One time read
  sbi(ADCSRA, 2);           // 0x100 = clock divided by 16
  cbi(ADCSRA, 1);        
  cbi(ADCSRA, 0);  

  write_twoByte(ADDR_DRV, 0x35, 0x1000); // Write 0b0001000000000000 (0x1000) to 0x35 arduino
  write_twoByte(ADDR_DRV, 0x60, 0x8000); // Write 0b1000000000000000 (0x8000) to 0x60 arduino
  write_twoByte(ADDR_DRV, 0x20, 0x3851); // Write 0b0011100001010001 (0x3851) to 0x20 arduino
  write_twoByte(ADDR_DRV, 0x21, 0x2d2d); // Write 0b0010110100101101 (0x2d2d) to 0x21 arduino
  write_twoByte(ADDR_DRV, 0x90, 0xc7bb); // Write 0b1100011110111011 (0xc7bb) to 0x90 arduino
  write_twoByte(ADDR_DRV, 0x91, 0x183b); // Write 0b0001100000111011 (0x183b) to 0x91 arduino
  write_twoByte(ADDR_DRV, 0x92, 0xff); // Write 0b0000000011111111 (0x00ff) to 0x92 arduino
  write_twoByte(ADDR_DRV, 0x93, 0x58ff); // Write 0b0101100011111111 (0x58ff) to 0x93 arduino
  write_twoByte(ADDR_DRV, 0x94, 0x6010); // Write 0b0110000000010000 (0x6010) to 0x94 arduino
  write_twoByte(ADDR_DRV, 0x95, 0x3f93); // Write 0b0011111110010011 (0x3f93) to 0x95 arduino
  write_twoByte(ADDR_DRV, 0x96, 0x480a); // Write 0b0100100000001010 (0x480a) to 0x96 arduino
  write_twoByte(ADDR_DRV, 0x60, 0x0); // Write 0b0000000000000000 (0x0000) to 0x60 arduino
  write_twoByte(ADDR_DRV, 0x30, 0x8008); // Write 0b1000000000001000 (0x8008) to 0x30 arduino
  //write_twoByte(ADDR_DRV, 0x30, 0x8010); // Write 0b1000000000010000 (0x8010) to 0x30 arduino
  write_twoByte(ADDR_DRV, 0x00, 0xffff); // Write 0b1111111111111111 (0xffff) to 0x00 arduino


  for(int i=0; i<3; i++)  
  {
    digitalWrite(LED3, LOW);  
    delay(100);
    digitalWrite(LED3, HIGH);  
    delay(100);
  }

}

uint16_t buffer[RANGE];       // buffer for histogram
uint8_t count=0;        // counter of half turns of mill

#define LOOPS 5

void adc()
{
  uint16_t u_sensor;
  uint16_t n = 0;

  while(true)
  {
    int16_t filtered = 0;
    uint8_t lo, hi;

    for(uint8_t j=0; j<8; j++)
    {
      sbi(ADCSRA, ADSC);        // ADC start conversions
      while (bit_is_clear(ADCSRA, ADIF)); // wait for end of conversion 

      lo = ADCL;
      hi = ADCH;
      sbi(ADCSRA, ADIF);            // reset interrupt flag from ADC

      u_sensor = ((hi) << 8) | (lo);      // combine the two bytes
      u_sensor &= 0x3FF;                  // pro sichr
      u_sensor >>= 1;                     // div 2
      filtered += u_sensor;      
    }
    filtered >>= 3;  // mean
    buffer[n++]=filtered;
    if (!digitalRead(EXTINT)) break;
    if (n>RANGE) break;
  }
  Serial1.print(buffer[0]);
  for(uint16_t i=1; i<n; i++)
  {
    Serial1.print(",");
    Serial1.print(buffer[i]);
  }
  Serial1.println();

#ifdef DEBUG
  for(uint16_t i=0; i<n; i++)
  {
    Serial.println(buffer[i]);
  }
  Serial.println("0");
#endif
}

void loop()
{
  while(true)
  {
    while(digitalRead(EXTINT)); delayMicroseconds(10);
    while(!digitalRead(EXTINT)); delayMicroseconds(10);
    adc();
    digitalWrite(LED1, !digitalRead(LED1));
    count++;   
    if (count==LOOPS) {count=0; digitalWrite(LED3, !digitalRead(LED3));};
  }
}
