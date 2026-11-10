#include "control_pi.h"
#include "config.h"
#include <math.h>

static float saturar(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

ControlPI pi_crear() {
  return {PI_KP, PI_KI, PI_TS, DUTY_MIN, DUTY_MAX, DUTY_MIN};
}

void pi_reiniciar(ControlPI& c, float duty) { c.integral = saturar(duty, c.umin, c.umax); }

float pi_paso(ControlPI& c, float t_cam, float t_sp) {
  if (isnan(t_cam)) return saturar(c.integral, c.umin, c.umax);
  float e = t_cam - t_sp;  // acción inversa: más calor, más ventilador
  float u = c.kp * e + c.integral;
  if ((u > c.umin && u < c.umax) || (u >= c.umax && e < 0) || (u <= c.umin && e > 0))
    c.integral = saturar(c.integral + c.ki * c.ts * e, c.umin, c.umax);
  return saturar(u, c.umin, c.umax);
}
