/*
  THUNDERMILL01 - field mill acquisition firmware
  ATmega1284P @ 16 MHz (external oscillator), MightyCore "standard" pinout.

  See board_pins.h for the full pin map.

  Output format (compatible with fw/arduino prototype):
    - ADC is sampled continuously over SPI and printed as 5-digit decimal.
    - Samples within one revolution are comma-separated.
    - A rising edge of the period detector (optical gate, PB2/INT2) ends the
      line with a newline.

  After STATUS_PERIOD_SAMPLES samples, the next revolution triggers a readout of
  the motor state, temperature and humidity, printed as a fixed-layout CSV line
  starting with "#S," so the data-stream parser can tell it apart:
    #S,<fault>,<fg>,<tempC>,<rh>        e.g.  #S,0,1,23.45,45.67
  fault and fg are 0/1, tempC/rh have 2 decimals or are "nan" on a sensor error.
  The readout blocks for a while (SHT31 conversion), so afterwards the firmware
  waits for the next revolution and restarts on a clean boundary.

  At startup the board serial number is read from the I2C EEPROM (0x58) and
  printed once as:
    #SN,<32 hex chars>                  ("nan" when the EEPROM does not answer)

  Serial0 (UART0): 115200 baud. Optiboot bootloader at 115200 baud.
  SHT31 hygrometer and serial number EEPROM on hardware I2C (PC0=SCL, PC1=SDA).
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

#include "board_pins.h"
#include "sht31.h"

static SHT31 sht31(SHT31_I2C_ADDR);
static bool sht31Ok = false;

// Board serial number read from the I2C EEPROM, as a hex string (2 chars/byte).
static char     serialNumber[2 * EEPROM_SN_LEN + 1] = "";
static uint32_t serialHash = 0;     // sum of the serial number bytes
static bool     serialOk = false;

// Status (motor / temperature / humidity) readout cadence, counted in samples.
static const uint16_t STATUS_PERIOD_SAMPLES = 20000;
static uint16_t sampleCount = 950;

// Period detector (optical gate) rising-edge flag, set from the ISR.
static volatile bool revolution = false;
static void revolutionISR() { revolution = true; }

static bool ledState = false;

// -----------------------------------------------------------------------------
// Note on timing
//   Timer0 is taken over for the motor PWM, so the Arduino millis()/delay()
//   (which use Timer0) are no longer valid. There is no other time base - all
//   cadences are counted in samples/revolutions, and the only blocking waits are
//   busy loops on delayMicroseconds().
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Motor PWM on PB3 = OC0A (Timer0), with soft-start ramp.
//   Phase-correct PWM, TOP = 0xFF (mode 1), non-inverting on OC0A, prescaler 1.
//   f = F_CPU / (presc * 510) = 16 MHz / 510 = 31.4 kHz. Duty = OCR0A / 255.
//   (Variable duty needs fixed TOP=255, so the frequency is 31.4 kHz rather than
//    exactly 20 kHz - the closest inaudible option on this pin.)
//   The Timer0 overflow (millis) interrupt is disabled in motorPwmInit().
// -----------------------------------------------------------------------------
static const uint8_t MOTOR_TARGET_DUTY = 28;       // ~11 % (of 255)

static void motorPwmInit()
{
  TIMSK0 &= ~_BV(TOIE0);                  // stop Timer0 millis ISR
  pinMode(PIN_MOTOR_PWM, OUTPUT);
  TCCR0A = _BV(COM0A1) | _BV(WGM00);      // non-inverting OC0A, phase-correct (mode 1)
  TCCR0B = _BV(CS00);                     // prescaler 1
  OCR0A  = MOTOR_TARGET_DUTY;             // start immediately at target duty
}


// -----------------------------------------------------------------------------
// Serial number readout from the I2C EEPROM (0x58).
//   Reads EEPROM_SN_LEN bytes from EEPROM_SN_REG and formats them into
//   serialNumber[] as lowercase hex; serialHash is their sum. On a bus error or
//   a short answer serialNumber[] is left empty and false is returned.
//   Must be called after Wire.begin().
// -----------------------------------------------------------------------------
static bool readSerialNumber()
{
  serialNumber[0] = '\0';
  serialHash = 0;

  Wire.beginTransmission(EEPROM_I2C_ADDR);        // request SN from EEPROM
  Wire.write((uint8_t)(EEPROM_SN_REG >> 8));      // MSB
  Wire.write((uint8_t)(EEPROM_SN_REG & 0xFF));    // LSB
  if (Wire.endTransmission() != 0) return false;  // no ACK - EEPROM not present

  if (Wire.requestFrom((uint8_t)EEPROM_I2C_ADDR, EEPROM_SN_LEN) != EEPROM_SN_LEN) return false;

  for (uint8_t reg = 0; reg < EEPROM_SN_LEN; reg++) {
    const uint8_t serialbyte = Wire.read();       // receive a byte
    sprintf(&serialNumber[2 * reg], "%02x", serialbyte);
    serialHash += serialbyte;
  }
  return true;
}


// -----------------------------------------------------------------------------
// Status readout, one fixed-layout CSV line:
//   #S,<fault>,<fg>,<tempC>,<rh>
// Always 5 comma-separated fields, always in this order. fault and fg are 0/1,
// tempC [degC] and rh [%] have 2 decimals, or "nan" when the SHT31 read failed.
// -----------------------------------------------------------------------------
static void printStatus()
{
  float tempC = 0.0f, rh = 0.0f;
  const bool envOk = sht31Ok && sht31.read(tempC, rh);

  Serial.print("#S,");
  Serial.print(digitalRead(PIN_MOTOR_FAULT) == LOW ? 1 : 0);
  Serial.print(',');
  Serial.print(digitalRead(PIN_MOTOR_FG));
  Serial.print(',');
  if (envOk) {
    Serial.print(tempC, 2);
    Serial.print(',');
    Serial.print(rh, 2);
  } else {
    Serial.print("nan,nan");
  }
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  Serial.println("#Hmmm...");
  Serial.println("#THUNDERMILL02,");


  // Motor controller
  pinMode(PIN_MOTOR_BRAKE, OUTPUT);
  pinMode(PIN_MOTOR_DIR, OUTPUT);
  digitalWrite(PIN_MOTOR_BRAKE, LOW);     // brake released
  digitalWrite(PIN_MOTOR_DIR, LOW);       // direction
  motorPwmInit();                         // 20 kHz, 50 % on PB3
  pinMode(PIN_MOTOR_FAULT, INPUT_PULLUP);
  pinMode(PIN_MOTOR_FG, INPUT);
  pinMode(PIN_MOTOR_SLEEP, OUTPUT);
  digitalWrite(PIN_MOTOR_SLEEP, HIGH);    // enable the motor driver

  // Acquisition: CONV trigger output, period detector input (interrupt)
  pinMode(PIN_ADC_CONV, OUTPUT);
  digitalWrite(PIN_ADC_CONV, HIGH);
  pinMode(PIN_PERIOD_SIGNAL, INPUT_PULLUP);

  // RS485
  pinMode(PIN_RS485_IN, INPUT);

  // SHT31 ALERT
  pinMode(PIN_HYGROMETER_ALERT, INPUT);

  // SPI: keep SS high so the hardware SPI stays in master mode
  pinMode(SPI_SS_PIN, OUTPUT);
  digitalWrite(SPI_SS_PIN, HIGH);
  SPI.begin();

  // SHT31 and the serial number EEPROM on hardware I2C (PC0=SCL, PC1=SDA)
  Wire.begin();
  sht31Ok = sht31.begin();
  serialOk = readSerialNumber();

  // Board identification line: #SN,<32 hex chars>  (or "nan" when unreadable)
  Serial.print("#SN,");
  Serial.println(serialOk ? serialNumber : "nan");

  // Period detector: newline on each rising edge (one revolution)
  attachInterrupt(digitalPinToInterrupt(PIN_PERIOD_SIGNAL), revolutionISR, RISING);

  for (uint8_t i = 0; i < 5; i++) {
    digitalWrite(PIN_LED1, HIGH);
    for (uint8_t j = 0; j < 120; j++) delayMicroseconds(1000);
    digitalWrite(PIN_LED1, LOW);
    for (uint8_t j = 0; j < 120; j++) delayMicroseconds(1000);
  }

  Serial.println("#Setup complete");
  Serial.flush();
}

void loop()
{
  while (PINB & _BV(PB2)) {}

  digitalWrite(PIN_ADC_CONV, LOW);
  const uint16_t adcVal = SPI.transfer16(0x8000);
  digitalWrite(PIN_ADC_CONV, HIGH);

  char buf[8];
  sprintf(buf, "%05u", adcVal); 
  Serial.print(buf);
  Serial.flush();


  if (revolution) {
    revolution = false;
    Serial.println();
    Serial.flush();
    sampleCount++;

    ledState = !ledState;
    digitalWrite(PIN_LED1, ledState ? HIGH : LOW);

    // Status STATUS_PERIOD_SAMPLES.
    if (sampleCount >= STATUS_PERIOD_SAMPLES) {
      sampleCount = 0;
      printStatus();

      revolution = false;
      while (!revolution) { }
      revolution = false;
    }
  } else {
    Serial.print(",");
    Serial.flush();
  }
}
