#include "ArduinoMavlink.h"
#include <HardwareSerial.h>

HardwareSerial &hs = Serial1;
ArduinoMavlink mav(hs);

unsigned long current_ms;

unsigned long heartbeat_previous_time = 0;
const long heartbeat_period = 500;
unsigned long data_previous_time = 0;
const long data_period = 1000;

boolean led_status = false;
//uint8_t data[] = "Hello World!";
uint8_t data[2];

void setup() {
  Serial.begin(57600);
  while(!mav.begin()){
    Serial.println("Not Connected!");
    delay(250);
  }
  mav.Stream();
  delay(2000);
}


void loop() {

//  periodical stuff
  current_ms = millis();
  
  if( current_ms > (heartbeat_previous_time + heartbeat_period)){
    heartbeat_previous_time = current_ms;
    
    mav.SendHeartBeat();
    // mav.SendTunnelData(data, 10, 0, 1, 1);
  }
  
  if( current_ms > (data_previous_time + data_period)){
    data_previous_time = current_ms;
    
    data[0] = random(255);
    data[1] = random(255);

    // data array (max length 128), data array size, data type (0 default - unknown), target sysid, target compid
    // For unicast (only for logging purposes) set sysid and compid to match the autopilot. For realtime visualisation, you can
    // set sysid and comid to broadcast (0, 0)
    mav.SendTunnelData(data, sizeof(data), 0, 0, 0);
    
    led_status = !led_status;
    digitalWrite(13, led_status);
  }

  delay(10);
}
