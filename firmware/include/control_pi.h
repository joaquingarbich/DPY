// PI discreto de acción inversa con integración condicional y acotada (§8.1).
#pragma once

struct ControlPI {
  float kp, ki, ts, umin, umax;
  float integral;
};

ControlPI pi_crear();                         // valores de config.h
void pi_reiniciar(ControlPI& c, float duty);  // transferencia sin salto
float pi_paso(ControlPI& c, float t_cam, float t_sp);
