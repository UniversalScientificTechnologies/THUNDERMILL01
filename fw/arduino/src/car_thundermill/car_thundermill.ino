// Thundermill for cars

// Compiled with: Arduino 1.8.13

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

#define RANGE 128   // size of output buffer

#include "wiring_private.h"
#include <Wire.h> 
          
#define LED1  13      //PD5         
#define LED2  14      //PD6         
#define LED3  15      //PD7         
#define COUNT1  27    //PA3       
#define COUNT2  20    //PC4         
#define TIMEPULSE  12 //PD4         
#define EXTINT  2     //PB2         

uint32_t serialhash = 0;
uint8_t lo, hi;
uint16_t u_sensor, maximum;

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

uint16_t CPS = 0;     // RPM
boolean flipflop = false; 

void counter()
{
  if (digitalRead(COUNT1)& flipflop)
  {
    digitalWrite(LED1,HIGH);
    flipflop=false;
    CPS++;
  };
  if (!digitalRead(COUNT1)& !flipflop)
  {
    digitalWrite(LED1,LOW);
    flipflop=true;
  };  
}


void setup()
{
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(COUNT1, INPUT);
  pinMode(COUNT2, INPUT);
  pinMode(TIMEPULSE, INPUT);

/*  debug output for oscilloscope
 *   
  pinMode(EXTINT, OUTPUT);
  digitalWrite(EXTINT, LOW);  

  digitalWrite(EXTINT, HIGH);
  digitalWrite(EXTINT, LOW);
*/

  for(int i=0; i<3; i++)  
  {
    digitalWrite(LED1, LOW);  
    delay(100);
    digitalWrite(LED1, HIGH);  
    delay(100);
  }
  
  Wire.setClock(100000);

  // Open serial communications
  Serial.begin(115200);

  for(int i=0; i<3; i++)  
  {
    digitalWrite(LED2, LOW);  
    delay(100);
    digitalWrite(LED2, HIGH);  
    delay(100);
  }

  ADMUX = (analog_reference << 6) | ((PIN | 0x10) & 0x1F);
  
  ADCSRB = 1;               // Switching ADC to One time read
  sbi(ADCSRA, 2);           // 0x100 = clock divided by 16
  cbi(ADCSRA, 1);        
  cbi(ADCSRA, 0);  

  for(int i=0; i<3; i++)  
  {
    digitalWrite(LED3, LOW);  
    delay(100);
    digitalWrite(LED3, HIGH);  
    delay(100);
  }

}

uint8_t buffer[RANGE];       // buffer for histogram
uint8_t count;        // counter of half turns of mill
uint8_t loop_c = 0;   // counter of mavlink packets
boolean edge = true;  // helper variable for rasing edge of half turn
uint8_t heart = 0;    // heartbeat

#define LOOPS 20

void loop()
{
  count = 0;
  float avg = 0;

  while(true)
  {
    uint8_t sensor;

    //if (!digitalRead(TIMEPULSE)) edge=true;

    counter();
    if (!digitalRead(COUNT2))
    {
      while(!digitalRead(COUNT2)) counter();
      digitalWrite(LED2, !digitalRead(LED2));
      {
        sbi(ADCSRA, ADSC);        // ADC start conversions
        while (bit_is_set(ADCSRA, ADSC)); // wait for end of conversion 
        //while (bit_is_clear(ADCSRA, ADIF)); // wait for end of conversion 
        lo = ADCL;
        hi = ADCH;
        //sbi(ADCSRA, ADIF);            // reset interrupt flag from ADC
  
  
        sensor = (bitRead(hi,0) << 7) | (lo >> 1);      // combine the two bytes
        if (!bitRead(hi,1)) 
        {
          sensor = 128+sensor; 
        }
        else
        {
          sensor = 128-(255-sensor); 
        }
        
        //buffer[count++] = sensor;
        count++;
        avg += sensor;
        //Serial.println(sensor);   // debug
      }
      //while(!digitalRead(COUNT2)) counter();
      
      Serial.println(sensor);
      if (count>(LOOPS-1)) break;
    }
  }
  avg = avg/LOOPS;
  //Serial.println(avg);
  delayMicroseconds(100);
  while(!digitalRead(COUNT2)) counter();

  edge=false;
  digitalWrite(LED3, !digitalRead(LED3));
      
}
