/*
  THUNDERMILL01 - field mill acquisition firmware
  ATmega1284P @ 16 MHz (external oscillator), MightyCore "standard" pinout.

  See board_pins.h for the full pin map.

  Output format (compatible with fw/arduino prototype):
    - ADC is sampled continuously over SPI and printed as 5-digit decimal.
    - Samples within one revolution are comma-separated.
    - A rising edge of the period detector (optical gate, PB2/INT2) ends the
      line with a newline.

  Once per minute, between periods (at a revolution boundary), the motor state,
  temperature and humidity are read and printed on a '#'-prefixed status line so
  the data-stream parser can skip it.

  Serial0 (UART0): 115200 baud. Optiboot bootloader at 115200 baud.
  SHT31 hygrometer on hardware I2C (PC0=SCL, PC1=SDA).
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

#include "board_pins.h"
#include "sht31.h"

// Self-test: when 1, fake revolutions are generated so the newline / status
// path can be verified without the optical gate or a spinning mill.
// Set back to 0 for normal operation.
#define EFM_SELFTEST 0

static SHT31 sht31(SHT31_I2C_ADDR);
static bool sht31Ok = false;

// Status (motor / temperature / humidity) readout cadence.
#if EFM_SELFTEST
static const uint32_t STATUS_PERIOD_MS = 5000UL;    // quick status for the demo
static const uint32_t SELFTEST_REV_MS  = 200UL;     // fake revolution period
#else
static const uint32_t STATUS_PERIOD_MS = 60000UL;   // once per minute
#endif
static uint32_t lastStatusMs = 0;

// Period detector (optical gate) rising-edge flag, set from the ISR.
static volatile bool revolution = false;
static void revolutionISR() { revolution = true; }

static bool ledState = false;

// -----------------------------------------------------------------------------
// Time base
//   Timer0 is taken over for the 20 kHz motor PWM, so the Arduino millis()/
//   delay() (which use Timer0) are no longer valid. A 1 ms tick is generated on
//   Timer2 instead and exposed via nowMs().
// -----------------------------------------------------------------------------
static volatile uint32_t g_ms = 0;

ISR(TIMER2_COMPA_vect) { g_ms++; }

static uint32_t nowMs()
{
  uint32_t m;
  uint8_t s = SREG;
  cli();
  m = g_ms;
  SREG = s;
  return m;
}

static void timebaseInit()
{
  // CTC, prescaler 64, TOP 249 -> 16 MHz / 64 / 250 = 1000 Hz (1 ms)
  TCCR2A = _BV(WGM21);
  TCCR2B = _BV(CS22);
  OCR2A  = 249;
  TIMSK2 = _BV(OCIE2A);
}

// -----------------------------------------------------------------------------
// Motor PWM on PB3 = OC0A (Timer0), with soft-start ramp.
//   Phase-correct PWM, TOP = 0xFF (mode 1), non-inverting on OC0A, prescaler 1.
//   f = F_CPU / (presc * 510) = 16 MHz / 510 = 31.4 kHz. Duty = OCR0A / 255.
//   (Variable duty needs fixed TOP=255, so the frequency is 31.4 kHz rather than
//    exactly 20 kHz - the closest inaudible option on this pin.)
//   The Timer0 overflow (millis) interrupt is disabled; see timebaseInit().
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
// ADC sampling over SPI (as in the fw/arduino prototype).
//   CONV (PB0) low triggers the conversion, then a 16-bit SPI transfer reads
//   the result. 0x8000 selects +/GND (0x0000 = +/-).
// -----------------------------------------------------------------------------
static uint16_t readAdcSample()
{
  digitalWrite(PIN_ADC_CONV, LOW);              // L on CONV
  const uint16_t v = SPI.transfer16(0x8000);    // 0x8000 +/GND, 0x0000 +/-
  digitalWrite(PIN_ADC_CONV, HIGH);
  return v;
}

// -----------------------------------------------------------------------------
// Status readout, printed between periods (own '#'-prefixed line).
// -----------------------------------------------------------------------------
static void printStatus()
{
  float tempC = 0.0f, rh = 0.0f;
  const bool envOk = sht31Ok && sht31.read(tempC, rh);

  Serial.print("# uptime=");
  Serial.print(nowMs() / 1000UL);
  Serial.print("s fault=");
  Serial.print(digitalRead(PIN_MOTOR_FAULT) == LOW ? 1 : 0);
  Serial.print(" fg=");
  Serial.print(digitalRead(PIN_MOTOR_FG));
  if (envOk) {
    Serial.print(" T=");
    Serial.print(tempC, 2);
    Serial.print(" RH=");
    Serial.print(rh, 2);
  } else {
    Serial.print(" T=err RH=err");
  }
  Serial.println();
}

void setup()
{
  Serial.begin(115200);

  // Status LED
  pinMode(PIN_LED1, OUTPUT);
  digitalWrite(PIN_LED1, LOW);

  // 1 ms time base on Timer2 (Timer0 is used by the motor PWM below)
  timebaseInit();

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

  // SHT31 on hardware I2C (PC0=SCL, PC1=SDA)
  Wire.begin();
  sht31Ok = sht31.begin();

  // Period detector: newline on each rising edge (one revolution)
  attachInterrupt(digitalPinToInterrupt(PIN_PERIOD_SIGNAL), revolutionISR, RISING);

  lastStatusMs = nowMs();

  // Startup indication: blink LED1 a few times.
  for (uint8_t i = 0; i < 5; i++) {
    digitalWrite(PIN_LED1, HIGH);
    for (uint8_t j = 0; j < 120; j++) delayMicroseconds(1000);   // ~120 ms
    digitalWrite(PIN_LED1, LOW);
    for (uint8_t j = 0; j < 120; j++) delayMicroseconds(1000);   // ~120 ms
  }

}

void loop()
{
  const uint16_t adcVal = readAdcSample();

  char buf[8];
  sprintf(buf, "%05u", adcVal);       // ADC as 5-digit zero-padded decimal
  Serial.print(buf);

#if EFM_SELFTEST
  static uint32_t lastRevMs = 0;
  if (nowMs() - lastRevMs >= SELFTEST_REV_MS) {
    lastRevMs = nowMs();
    revolution = true;
  }
#endif

  if (revolution) {
    revolution = false;
    Serial.println();                       // rising edge -> end of revolution

    ledState = !ledState;                   // activity LED, one toggle per revolution
    digitalWrite(PIN_LED1, ledState ? HIGH : LOW);

    // Status readout once per minute, performed between periods.
    if (nowMs() - lastStatusMs >= STATUS_PERIOD_MS) {
      lastStatusMs = nowMs();
      printStatus();
    }
  } else {
    Serial.print(",");
  }
}
