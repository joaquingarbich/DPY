#include <unity.h>
#include <math.h>
#include "fin_ciclo.h"

void setUp() {}
void tearDown() {}

static const uint32_t H = 3600;

void test_masa_objetivo() {
  // m0 = 6000 g, x0 = 0,83, xf = 0,17 → m_ss = 1020 g, m_obj ≈ 1228,9 g
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 1228.9f, masa_objetivo(6000, 0.83f, 0.17f));
}

void test_tres_registros_bajo_m_obj() {
  FinCiclo f;
  TEST_ASSERT_FALSE(fin_ciclo_paso(f, 1000, 1035, true, 20 * H));
  TEST_ASSERT_FALSE(fin_ciclo_paso(f, 999, 1035, true, 20 * H));
  TEST_ASSERT_TRUE(fin_ciclo_paso(f, 998, 1035, true, 20 * H));
}

void test_registro_aislado_no_dispara() {
  FinCiclo f;
  TEST_ASSERT_FALSE(fin_ciclo_paso(f, 1000, 1035, true, 20 * H));
  TEST_ASSERT_FALSE(fin_ciclo_paso(f, 1040, 1035, true, 20 * H));  // vuelve arriba
  TEST_ASSERT_FALSE(fin_ciclo_paso(f, 1000, 1035, true, 20 * H));
  TEST_ASSERT_FALSE(fin_ciclo_paso(f, NAN, 1035, true, 20 * H));   // NAN corta la racha
  TEST_ASSERT_FALSE(fin_ciclo_paso(f, 1000, 1035, true, 20 * H));
}

void test_provisional_no_dispara_antes_de_12h() {
  FinCiclo f;
  for (int i = 0; i < 10; i++) TEST_ASSERT_FALSE(fin_ciclo_paso(f, 900, 1035, false, 11 * H));
  TEST_ASSERT_FALSE(fin_ciclo_paso(f, 900, 1035, false, 12 * H));
  TEST_ASSERT_FALSE(fin_ciclo_paso(f, 900, 1035, false, 12 * H + 60));
  TEST_ASSERT_TRUE(fin_ciclo_paso(f, 900, 1035, false, 12 * H + 120));
  // Con x0 de laboratorio no hay inhibición
  FinCiclo g;
  fin_ciclo_paso(g, 900, 1035, true, 1 * H);
  fin_ciclo_paso(g, 900, 1035, true, 1 * H);
  TEST_ASSERT_TRUE(fin_ciclo_paso(g, 900, 1035, true, 1 * H));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_masa_objetivo);
  RUN_TEST(test_tres_registros_bajo_m_obj);
  RUN_TEST(test_registro_aislado_no_dispara);
  RUN_TEST(test_provisional_no_dispara_antes_de_12h);
  return UNITY_END();
}
