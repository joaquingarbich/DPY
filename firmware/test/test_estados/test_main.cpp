#include <unity.h>
#include "estados.h"
#include "config.h"

void setUp() {}
void tearDown() {}

// Simulador mínimo: un paso de 10 s con condiciones de día normales.
struct Sim {
  Maquina m;
  EntradaControl e{};
  SalidaControl s{};
  uint32_t t = 1000;
  Sim() {
    e.estable = true; e.masa_g = 3000; e.cam_valida = true; e.t_cam_ctrl = 60;
    e.g_est = 600; e.dt_col = 15;
  }
  void paso(Boton b = Boton::NINGUNO) {
    t += 10;
    e.ahora_s = t; e.nueva_lectura = true; e.boton = b;
    maquina_paso(m, e, 60, s);
    e.fin_detectado = false;
  }
  void boton(Boton b, uint32_t dt = 1) {  // pulsación entre lecturas
    t += dt;
    e.ahora_s = t; e.nueva_lectura = false; e.boton = b;
    maquina_paso(m, e, 60, s);
  }
  bool hubo(TipoEvento t) {
    for (int i = 0; i < s.n_ev; i++) if (s.ev[i] == t) return true;
    return false;
  }
  void a_operacion() { boton(Boton::CORTA); boton(Boton::LARGA); }
};

void test_flujo_normal() {
  Sim s;
  TEST_ASSERT_EQUAL(Estado::ESPERA, s.m.estado);
  TEST_ASSERT_FALSE(s.s.ventilador);
  s.boton(Boton::CORTA);
  TEST_ASSERT_EQUAL(Estado::CARGA, s.m.estado);
  TEST_ASSERT_TRUE(s.hubo(TipoEvento::INICIO_LOTE));
  s.boton(Boton::LARGA);
  TEST_ASSERT_EQUAL(Estado::OPERACION, s.m.estado);
  TEST_ASSERT_TRUE(s.hubo(TipoEvento::MASA_INICIAL));
  TEST_ASSERT_TRUE(s.s.ventilador);
  TEST_ASSERT_EQUAL_FLOAT(30, s.s.duty);         // arranque sin salto
  s.e.fin_detectado = true;
  s.paso();
  TEST_ASSERT_EQUAL(Estado::FIN_CICLO, s.m.estado);
  TEST_ASSERT_TRUE(s.hubo(TipoEvento::FIN_CICLO));
  s.boton(Boton::LARGA);
  TEST_ASSERT_EQUAL(Estado::ESPERA, s.m.estado);
  TEST_ASSERT_TRUE(s.hubo(TipoEvento::CIERRE_LOTE));
  TEST_ASSERT_FALSE(s.s.ventilador);
}

void test_condiciones_de_entrada() {
  Sim s;
  s.e.estable = false;
  s.boton(Boton::CORTA);
  TEST_ASSERT_EQUAL(Estado::ESPERA, s.m.estado);   // sin lectura estable no hay tara
  s.e.estable = true;
  s.boton(Boton::CORTA);
  s.e.masa_g = 400;
  s.boton(Boton::LARGA);
  TEST_ASSERT_EQUAL(Estado::CARGA, s.m.estado);    // masa ≤ 500 g
  s.e.masa_g = 3000;
  s.boton(Boton::CORTA);
  TEST_ASSERT_EQUAL(Estado::CARGA, s.m.estado);    // corta no confirma
}

void test_carga_cancelada_30_min() {
  Sim s;
  s.boton(Boton::CORTA);
  for (int i = 0; i < 179; i++) s.paso();
  TEST_ASSERT_EQUAL(Estado::CARGA, s.m.estado);
  s.paso();
  TEST_ASSERT_EQUAL(Estado::ESPERA, s.m.estado);
  TEST_ASSERT_TRUE(s.hubo(TipoEvento::CARGA_CANCELADA));
}

void test_cierre_doble_larga() {
  Sim s;
  s.a_operacion();
  s.boton(Boton::LARGA);
  TEST_ASSERT_EQUAL(Estado::OPERACION, s.m.estado);  // una sola no cierra
  s.paso(); s.paso();                                  // 20 s después
  s.boton(Boton::LARGA);                               // fuera de los 10 s: cuenta como primera
  TEST_ASSERT_EQUAL(Estado::OPERACION, s.m.estado);
  s.boton(Boton::LARGA, 5);
  TEST_ASSERT_EQUAL(Estado::ESPERA, s.m.estado);
  TEST_ASSERT_TRUE(s.hubo(TipoEvento::CIERRE_MANUAL));
}

void test_modo_seguro_y_vuelta() {
  Sim s;
  s.a_operacion();
  s.e.cam_valida = false;
  s.paso(); s.paso();
  TEST_ASSERT_EQUAL(Estado::OPERACION, s.m.estado);
  s.paso();
  TEST_ASSERT_EQUAL(Estado::MODO_SEGURO, s.m.estado);
  TEST_ASSERT_TRUE(s.hubo(TipoEvento::MODO_SEGURO));
  TEST_ASSERT_EQUAL_FLOAT(100, s.s.duty);
  s.boton(Boton::LARGA); s.boton(Boton::LARGA);
  TEST_ASSERT_EQUAL(Estado::MODO_SEGURO, s.m.estado);  // los botones no actúan
  s.e.cam_valida = true;
  for (int i = 0; i < 5; i++) s.paso();
  TEST_ASSERT_EQUAL(Estado::MODO_SEGURO, s.m.estado);
  s.paso();
  TEST_ASSERT_EQUAL(Estado::OPERACION, s.m.estado);
  TEST_ASSERT_TRUE(s.hubo(TipoEvento::FIN_MODO_SEGURO));
  TEST_ASSERT_TRUE(s.s.duty > 90);                     // sin salto desde 100 %
}

void test_modo_seguro_desde_fin_ciclo() {
  Sim s;
  s.a_operacion();
  s.e.fin_detectado = true; s.paso();
  s.e.cam_valida = false; s.paso(); s.paso(); s.paso();
  TEST_ASSERT_EQUAL(Estado::MODO_SEGURO, s.m.estado);
  s.e.cam_valida = true;
  for (int i = 0; i < 6; i++) s.paso();
  TEST_ASSERT_EQUAL(Estado::FIN_CICLO, s.m.estado);
}

void test_pausa() {
  Sim s;
  s.a_operacion();
  s.e.g_est = 50; s.e.dt_col = 1;                // noche
  for (int i = 0; i < 60; i++) s.paso();         // 600 s desde la primera
  TEST_ASSERT_FALSE(s.m.pausa);
  s.paso();
  TEST_ASSERT_TRUE(s.m.pausa);
  TEST_ASSERT_TRUE(s.hubo(TipoEvento::PAUSA_INICIO));
  TEST_ASSERT_FALSE(s.s.ventilador);
  TEST_ASSERT_EQUAL_FLOAT(0, s.s.duty);
  s.e.g_est = 200;                               // amanece
  for (int i = 0; i < 12; i++) s.paso();
  TEST_ASSERT_TRUE(s.m.pausa);
  s.paso();
  TEST_ASSERT_FALSE(s.m.pausa);
  TEST_ASSERT_TRUE(s.hubo(TipoEvento::PAUSA_FIN));
  TEST_ASSERT_TRUE(s.s.ventilador);
  TEST_ASSERT_EQUAL_FLOAT(30, s.s.duty);         // integral reiniciado en 30 %
}

void test_pausa_se_interrumpe() {
  Sim s;
  s.a_operacion();
  s.e.g_est = 50; s.e.dt_col = 1;
  for (int i = 0; i < 50; i++) s.paso();
  s.e.dt_col = 4; s.paso();                      // corta la condición
  s.e.dt_col = 1;
  for (int i = 0; i < 50; i++) s.paso();
  TEST_ASSERT_FALSE(s.m.pausa);
}

void test_ventilador_en_fin_ciclo() {
  Sim s;
  s.a_operacion();
  s.e.fin_detectado = true; s.paso();
  TEST_ASSERT_TRUE(s.s.ventilador);
  TEST_ASSERT_EQUAL_FLOAT(30, s.s.duty);
  s.e.g_est = 120; s.paso();                     // entre 100 y 150: mantiene
  TEST_ASSERT_TRUE(s.s.ventilador);
  s.e.g_est = 80;
  for (int i = 0; i < 6; i++) s.paso();
  TEST_ASSERT_TRUE(s.s.ventilador);
  s.paso();                                      // 60 s bajo 100 W/m²
  TEST_ASSERT_FALSE(s.s.ventilador);
  s.e.g_est = 120; s.paso();
  TEST_ASSERT_FALSE(s.s.ventilador);
  s.e.g_est = 160; s.paso();
  TEST_ASSERT_TRUE(s.s.ventilador);
  TEST_ASSERT_EQUAL_FLOAT(30, s.s.duty);
}

void test_pi_actua_en_operacion() {
  Sim s;
  s.a_operacion();
  s.e.t_cam_ctrl = 70;
  for (int i = 0; i < 10; i++) s.paso();
  TEST_ASSERT_TRUE(s.s.duty > 30);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_flujo_normal);
  RUN_TEST(test_condiciones_de_entrada);
  RUN_TEST(test_carga_cancelada_30_min);
  RUN_TEST(test_cierre_doble_larga);
  RUN_TEST(test_modo_seguro_y_vuelta);
  RUN_TEST(test_modo_seguro_desde_fin_ciclo);
  RUN_TEST(test_pausa);
  RUN_TEST(test_pausa_se_interrumpe);
  RUN_TEST(test_ventilador_en_fin_ciclo);
  RUN_TEST(test_pi_actua_en_operacion);
  return UNITY_END();
}
