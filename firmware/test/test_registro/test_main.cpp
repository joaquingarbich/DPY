#include <unity.h>
#include <math.h>
#include <string.h>
#include <ArduinoJson.h>
#include "registro_json.h"

void setUp() {}
void tearDown() {}

static Registro ejemplo() {
  Registro r{};
  strcpy(r.ts, "2026-10-01T21:07:00Z");
  r.seq = 512; r.masa_g = 2873.4f; r.t_cam = 54.62f; r.hr_cam = 23.1f;
  r.t_ent = 31.18f; r.hr_ent = 47.0f; r.t_col = 63.94f; r.luz_lux = 41260;
  r.duty = 30; r.estado = Estado::OPERACION; r.pausa = false; r.n_descartes = 0; r.senal_dbm = -61;
  return r;
}

void test_json_valido() {
  char buf[400];
  size_t n = registro_a_json(ejemplo(), "L-02", buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0 && n < 400);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, buf));
  TEST_ASSERT_EQUAL_STRING("SEC-01", d["eq"]);
  TEST_ASSERT_EQUAL_STRING("1.1.0", d["fw"]);
  TEST_ASSERT_EQUAL_STRING("L-02", d["lote"]);
  TEST_ASSERT_EQUAL(512, d["seq"].as<int>());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 54.62f, d["t_cam"].as<float>());
  TEST_ASSERT_EQUAL_STRING("operacion", d["estado"]);
  TEST_ASSERT_FALSE(d["pausa"].as<bool>());
  TEST_ASSERT_EQUAL(-61, d["senal_dbm"].as<int>());
}

void test_campos_null() {
  Registro r = ejemplo();
  r.ts[0] = '\0'; r.t_col = NAN; r.masa_g = NAN; r.senal_dbm = SIN_SENAL;
  char buf[400];
  TEST_ASSERT_TRUE(registro_a_json(r, "L-02", buf, sizeof buf) > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, buf));
  TEST_ASSERT_TRUE(d["ts"].isNull());
  TEST_ASSERT_TRUE(d["t_col"].isNull());
  TEST_ASSERT_TRUE(d["masa_g"].isNull());
  TEST_ASSERT_FALSE(d["t_cam"].isNull());
  TEST_ASSERT_TRUE(d["senal_dbm"].isNull());
}

void test_tamano_peor_caso() {
  Registro r = ejemplo();
  r.seq = 4294967295u; r.masa_g = -10500.5f; r.t_cam = -10.55f; r.t_ent = -10.55f;
  r.t_col = 125.55f; r.hr_cam = 100; r.hr_ent = 100; r.luz_lux = 65535; r.duty = 100;
  r.estado = Estado::MODO_SEGURO; r.pausa = true; r.n_descartes = 65535; r.senal_dbm = -128;
  char buf[400];
  size_t n = registro_a_json(r, "L-99999999", buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0 && n < 400);
  TEST_ASSERT_EQUAL(0, registro_a_json(r, "L-02", buf, 50));  // no cabe → 0
}

void test_evento() {
  char buf[256];
  TEST_ASSERT_TRUE(evento_a_json(TipoEvento::FIN_CICLO, "L-02", "2026-10-01T21:07:00Z",
                                 "{\"masa_g\":1034.1,\"m_obj_g\":1035.0}", buf, sizeof buf) > 0);
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, buf));
  TEST_ASSERT_EQUAL_STRING("fin_ciclo", d["tipo"]);
  TEST_ASSERT_EQUAL_STRING("critico", d["nivel"]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1035.0f, d["datos"]["m_obj_g"].as<float>());
  evento_a_json(TipoEvento::CIERRE_MANUAL, nullptr, nullptr, nullptr, buf, sizeof buf);
  d.clear();
  TEST_ASSERT_FALSE(deserializeJson(d, buf));
  TEST_ASSERT_EQUAL_STRING("cierre_lote", d["tipo"]);
  TEST_ASSERT_TRUE(d["lote"].isNull());
}

static ParametrosLote base() { return {0.83f, false, 0.17f, 60.0f, 60}; }

void test_config() {
  ParametrosLote p = base();
  const char* ok = "{\"lote\":\"L-02\",\"x0\":0.8281,\"x0_lab\":true,\"xf\":0.170,\"setpoint\":58,\"intervalo_s\":120}";
  TEST_ASSERT_EQUAL(ResultadoConfig::APLICADA, config_desde_json(ok, strlen(ok), "L-02", p));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.8281f, p.x0);
  TEST_ASSERT_TRUE(p.x0_lab);
  TEST_ASSERT_EQUAL(120, p.intervalo_s);
  TEST_ASSERT_EQUAL(ResultadoConfig::SIN_CAMBIOS, config_desde_json(ok, strlen(ok), "L-02", p));

  // x0 sin x0_lab → provisional
  ParametrosLote q = base();
  const char* prov = "{\"lote\":\"L-02\",\"x0\":0.82,\"setpoint\":55}";
  TEST_ASSERT_EQUAL(ResultadoConfig::APLICADA, config_desde_json(prov, strlen(prov), "L-02", q));
  TEST_ASSERT_FALSE(q.x0_lab);

  // fuera de rango → se rechaza entero, p no cambia
  ParametrosLote r = base();
  const char* malo = "{\"lote\":\"L-02\",\"x0\":0.80,\"setpoint\":70}";
  TEST_ASSERT_EQUAL(ResultadoConfig::RECHAZADA, config_desde_json(malo, strlen(malo), "L-02", r));
  TEST_ASSERT_EQUAL_FLOAT(0.83f, r.x0);
  const char* iv = "{\"lote\":\"L-02\",\"intervalo_s\":65596}";
  TEST_ASSERT_EQUAL(ResultadoConfig::RECHAZADA, config_desde_json(iv, strlen(iv), "L-02", r));
  const char* iv_max = "{\"lote\":\"L-02\",\"intervalo_s\":1800}";
  TEST_ASSERT_EQUAL(ResultadoConfig::APLICADA, config_desde_json(iv_max, strlen(iv_max), "L-02", r));
  TEST_ASSERT_EQUAL(1800, r.intervalo_s);
  const char* iv_alto = "{\"lote\":\"L-02\",\"intervalo_s\":1860}";
  TEST_ASSERT_EQUAL(ResultadoConfig::RECHAZADA, config_desde_json(iv_alto, strlen(iv_alto), "L-02", r));

  // otro lote o sin lote activo → se ignora
  TEST_ASSERT_EQUAL(ResultadoConfig::IGNORADA, config_desde_json(ok, strlen(ok), "L-03", r));
  TEST_ASSERT_EQUAL(ResultadoConfig::IGNORADA, config_desde_json(ok, strlen(ok), "", r));
  TEST_ASSERT_EQUAL(ResultadoConfig::RECHAZADA, config_desde_json("{x", 2, "L-02", r));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_json_valido);
  RUN_TEST(test_campos_null);
  RUN_TEST(test_tamano_peor_caso);
  RUN_TEST(test_evento);
  RUN_TEST(test_config);
  return UNITY_END();
}
