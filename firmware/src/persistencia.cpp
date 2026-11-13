#include "persistencia.h"
#include "config.h"
#include <Preferences.h>
#include <string.h>

static Preferences p;

void nvs_iniciar() { p.begin("secador", false); }

Calibracion nvs_cargar_cal() {
  return {p.getFloat("cal_S", CAL_S_DEF), p.getFloat("cal_CT", CAL_CT_DEF), p.getFloat("cal_K", CAL_K_DEF)};
}

void nvs_guardar_cal(const char* clave, float v) { p.putFloat(clave, v); }

bool nvs_cargar_lote(Lote& l, ParametrosLote& q) {
  if (!p.isKey("lote_id")) return false;
  Estado e = (Estado)p.getUChar("lote_est", (uint8_t)Estado::ESPERA);
  if (e != Estado::OPERACION && e != Estado::FIN_CICLO && e != Estado::MODO_SEGURO) return false;
  memset(&l, 0, sizeof l);
  p.getString("lote_id", l.id, sizeof l.id);
  l.estado = e;
  l.tara_cuentas = p.getFloat("tara", 0);
  l.m0_g = p.getFloat("m0", 0);
  l.seq = p.getUInt("seq", 0);
  l.t_inicio = p.getUInt("t_ini", 0);
  q.x0 = p.getFloat("x0", X0_PROV);
  q.x0_lab = p.getBool("x0_lab", false);
  q.xf = p.getFloat("xf", XF_DEF);
  q.setpoint = p.getFloat("sp", SP_DEF);
  q.intervalo_s = p.getUShort("int", INTERVALO_DEF_S);
  return true;
}

void nvs_guardar_lote(const Lote& l, const ParametrosLote& q) {
  p.putString("lote_id", l.id);
  // MODO_SEGURO no se guarda: tras un reinicio se retoma el estado previo y,
  // si el SHT31 sigue fallando, vuelve a modo seguro en 3 min.
  if (l.estado != Estado::MODO_SEGURO) p.putUChar("lote_est", (uint8_t)l.estado);
  p.putFloat("tara", l.tara_cuentas);
  p.putFloat("m0", l.m0_g);
  p.putUInt("seq", l.seq);
  p.putUInt("t_ini", l.t_inicio);
  p.putFloat("x0", q.x0);
  p.putBool("x0_lab", q.x0_lab);
  p.putFloat("xf", q.xf);
  p.putFloat("sp", q.setpoint);
  p.putUShort("int", q.intervalo_s);
}

void nvs_guardar_seq(uint32_t seq) { p.putUInt("seq", seq); }

void nvs_borrar_lote() {
  p.remove("lote_id");
  p.putUChar("lote_est", (uint8_t)Estado::ESPERA);
}

uint16_t nvs_n_lote() { return p.getUShort("n_lote", 0); }
void nvs_guardar_n_lote(uint16_t n) { p.putUShort("n_lote", n); }
