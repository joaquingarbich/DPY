#include <unity.h>
#include "control_pi.h"

void setUp() {}
void tearDown() {}

void test_satura_en_100() {
  ControlPI c = pi_crear();
  pi_reiniciar(c, 0);
  float u = 0;
  for (int i = 0; i < 500; i++) u = pi_paso(c, 80, 60);  // muy caliente
  TEST_ASSERT_EQUAL_FLOAT(100, u);
  TEST_ASSERT_TRUE(c.integral <= 100);
}

void test_satura_en_30() {
  ControlPI c = pi_crear();
  pi_reiniciar(c, 100);
  float u = 100;
  for (int i = 0; i < 500; i++) u = pi_paso(c, 30, 60);  // muy frío
  TEST_ASSERT_EQUAL_FLOAT(30, u);
  TEST_ASSERT_TRUE(c.integral >= 30);
}

void test_integral_acotado_y_recupera() {
  ControlPI c = pi_crear();
  pi_reiniciar(c, 0);
  for (int i = 0; i < 2000; i++) pi_paso(c, 90, 60);
  TEST_ASSERT_TRUE(c.integral >= 30 && c.integral <= 100);
  // Sin windup: al caer bajo el setpoint el duty baja del 100 % en el primer paso.
  float u = pi_paso(c, 50, 60);
  TEST_ASSERT_TRUE(u < 100);
}

void test_transferencia_sin_salto() {
  ControlPI c = pi_crear();
  pi_reiniciar(c, 0);                    // ventilador apagado → encendido
  TEST_ASSERT_EQUAL_FLOAT(30, c.integral);
  TEST_ASSERT_EQUAL_FLOAT(30, pi_paso(c, 60, 60));  // error cero: sale en 30 %
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_satura_en_100);
  RUN_TEST(test_satura_en_30);
  RUN_TEST(test_integral_acotado_y_recupera);
  RUN_TEST(test_transferencia_sin_salto);
  return UNITY_END();
}
