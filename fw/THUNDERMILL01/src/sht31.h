/*
  Minimal SHT31 (Sensirion) driver over hardware I2C (Wire).
  Single-shot, high repeatability, no clock stretching.

  No external library dependency. Returns false on I2C / CRC error.
*/

#ifndef THUNDERMILL01_SHT31_H
#define THUNDERMILL01_SHT31_H

#include <Arduino.h>
#include <Wire.h>

class SHT31 {
public:
  explicit SHT31(uint8_t addr = 0x44) : _addr(addr) {}

  // Wire.begin() must be called by the caller beforehand.
  bool begin() {
    return softReset();
  }

  bool softReset() {
    return sendCommand(0x30A2);
  }

  // Reads temperature [degC] and relative humidity [%]. Blocks ~16 ms.
  bool read(float &tempC, float &humidity) {
    if (!sendCommand(0x2400)) {           // single shot, high repeatability
      return false;
    }
    // ~16 ms measurement time. delayMicroseconds() is a busy loop and does not
    // depend on Timer0 (which is used by the motor PWM), unlike delay().
    for (uint8_t i = 0; i < 16; i++) {
      delayMicroseconds(1000);
    }

    if (Wire.requestFrom(_addr, (uint8_t)6) != 6) {
      return false;
    }
    uint8_t b[6];
    for (uint8_t i = 0; i < 6; i++) {
      b[i] = Wire.read();
    }
    if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) {
      return false;                        // CRC mismatch
    }

    const uint16_t rawT = (uint16_t(b[0]) << 8) | b[1];
    const uint16_t rawH = (uint16_t(b[3]) << 8) | b[4];
    tempC    = -45.0f + 175.0f * (float)rawT / 65535.0f;
    humidity = 100.0f * (float)rawH / 65535.0f;
    return true;
  }

private:
  bool sendCommand(uint16_t cmd) {
    Wire.beginTransmission(_addr);
    Wire.write((uint8_t)(cmd >> 8));
    Wire.write((uint8_t)(cmd & 0xFF));
    return Wire.endTransmission() == 0;
  }

  static uint8_t crc8(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0xFF;                     // polynomial 0x31, init 0xFF
    for (uint8_t i = 0; i < len; i++) {
      crc ^= data[i];
      for (uint8_t bit = 0; bit < 8; bit++) {
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
      }
    }
    return crc;
  }

  uint8_t _addr;
};

#endif // THUNDERMILL01_SHT31_H
