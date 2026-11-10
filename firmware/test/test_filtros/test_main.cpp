#include <unity.h>
#include <math.h>
#include "filtros.h"

void setUp() {}
void tearDown() {}

void test_mediana_con_atipicos() {
  float v[] = {1000, 1001, 999, 1500, 1000, 400, 1002, 998};
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000, mediana(v, 8));  // 400 y 1500 no la mueven
  float impar[] = {10, 9000, 11};
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 11, mediana(impar, 3));
}

void test_ventana_con_nan() {
  float v[] = {NAN, 20, NAN, 22, 24, NAN};
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22, mediana(v, 6));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 22, media(v, 6));
  float todos[] = {NAN, NAN};
  TEST_ASSERT_TRUE(isnan(mediana(todos, 2)));
  TEST_ASSERT_TRUE(isnan(media(todos, 2)));
}

void test_masa_intervalo_descarta() {
  float v[] = {2000, 2001, 2040, 1999, NAN, 2002};  // 2040 se aleja más de 15 g
  uint16_t d = 0;
  float m = masa_intervalo(v, 6, 15, &d);
  TEST_ASSERT_EQUAL_UINT16(2, d);                  // un atípico + un NAN
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 2000.5f, m);
}

void test_desviacion() {
  float quieto[] = {500, 500.5f, 499.5f, 500, 500};
  TEST_ASSERT_TRUE(desviacion(quieto, 5) < 2);
  float movido[] = {500, 520, 480, 510, 490};
  TEST_ASSERT_TRUE(desviacion(movido, 5) > 2);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mediana_con_atipicos);
  RUN_TEST(test_ventana_con_nan);
  RUN_TEST(test_masa_intervalo_descarta);
  RUN_TEST(test_desviacion);
  return UNITY_END();
}
