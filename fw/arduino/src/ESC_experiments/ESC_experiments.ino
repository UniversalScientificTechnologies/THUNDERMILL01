#include <Wire.h>

const char* motor_states[] = {
  "MOTOR_IDLE",                      // 0h
  "MOTOR_ISD",                       // 1h
  "MOTOR_TRISTATE",                  // 2h
  "MOTOR_BRAKE_ON_START",            // 3h
  "MOTOR_IPD",                       // 4h
  "MOTOR_SLOW_FIRST_CYCLE",          // 5h
  "MOTOR_ALIGN",                     // 6h
  "MOTOR_OPEN_LOOP",                 // 7h
  "MOTOR_CLOSED_LOOP_UNALIGNED",     // 8h
  "MOTOR_CLOSED_LOOP_ALIGNED",       // 9h
  "MOTOR_CLOSED_LOOP_ACTIVE_BRAKING",// Ah
  "MOTOR_SOFT_STOP",                 // Bh
  "MOTOR_RECIRCULATE_STOP",          // Ch
  "MOTOR_BRAKE_ON_STOP",             // Dh
  "MOTOR_FAULT",                     // Eh
  "MOTOR_MPET_MOTOR_STOP_CHECK",     // Fh
  "MOTOR_MPET_MOTOR_STOP_WAIT",      // 10h
  "MOTOR_MPET_MOTOR_BRAKE",          // 11h
  "MOTOR_MPET_ALGORITHM_PARAMETERS_INIT", // 12h
  "MOTOR_MPET_RL_MEASURE",           // 13h
  "MOTOR_MPET_KE_MEASURE",           // 14h
  "MOTOR_MPET_STALL_CURRENT_MEASURE",// 15h
  "MOTOR_MPET_TORQUE_MODE",          // 16h
  "MOTOR_MPET_DONE",                 // 17h
  "MOTOR_MPET_FAULT"                 // 18h
};

void setup()
{
  Serial.begin(115200);
  Wire.begin();
  Wire.setClock(10000);
}

void loop()
{
  // 1. Napájecí napětí (VM_VOLTAGE)
  unsigned long vm_raw = read32(0x478);
  float voltage = (float)vm_raw * 60.0 / 134217728.0;
  Serial.print("Napájecí napětí (VM) = ");
  Serial.print(voltage, 3);
  Serial.println(" V");

  // 2. Odběr proudu (BUS_CURRENT)
  unsigned long bus_current_raw = read32(0x410);
  float current = (float)bus_current_raw / 134217728.0 * 1.25;
  Serial.print("Odběr proudu (Ibus) = ");
  Serial.print(current, 4);
  Serial.println(" A");

  // 3. Otáčky (FG_SPEED_FDBK)
  unsigned long speed_raw = read32(0x216);
  const float MAX_SPEED = 500.0; // dosaď skutečnou hodnotu
  float speed_hz = (float)speed_raw / 134217728.0 * MAX_SPEED;
  Serial.print("Otáčky (FG_SPEED_FDBK) = ");
  Serial.print(speed_hz, 2);
  Serial.println(" Hz");

  // 4. Stav algoritmu (ALGORITHM_STATE)
  unsigned long alg_state_raw = read32(0x210);
  uint16_t alg_state = alg_state_raw & 0xFFFF; // spodních 16 bitů
  Serial.print("Stav algoritmu (ALGORITHM_STATE) = 0x");
  Serial.print(alg_state, HEX);
  Serial.print(" = ");
  if (alg_state < sizeof(motor_states)/sizeof(motor_states[0]))
    Serial.println(motor_states[alg_state]);
  else
    Serial.println("NEZNÁMÝ_STAV");

  Serial.println("------");
  delay(1000);
}

unsigned long read32(int reg_addr)
{
  byte reg_addr_H = (reg_addr & 0x0F00) >> 8;
  byte reg_addr_L = (reg_addr & 0xFF);
  const byte control_word[] = {0x90, reg_addr_H, reg_addr_L};

  Wire.beginTransmission(0x01);
  Wire.write(control_word, 3);
  Wire.endTransmission();

  Wire.requestFrom(0x01, 4);

  unsigned long register_value = 0;
  for (int i = 0; i < 4; i++)
  {
    unsigned long c = Wire.read();
    register_value |= (c << (i * 8));
  }
  return register_value;
}
