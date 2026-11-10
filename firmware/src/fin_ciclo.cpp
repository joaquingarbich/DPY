#include "fin_ciclo.h"
#include "config.h"
#include <math.h>

float masa_objetivo(float m0, float x0, float xf) {
  float m_ss = m0 * (1 - x0);
  return m_ss / (1 - xf);
}

bool fin_ciclo_paso(FinCiclo& f, float masa_g, float m_obj_g, bool x0_lab, uint32_t s_desde_inicio) {
  if (!x0_lab && s_desde_inicio < INHIBICION_PROV_S) { f.bajo = 0; return false; }
  if (isnan(masa_g) || masa_g >= m_obj_g) { f.bajo = 0; return false; }
  if (f.bajo < REG_BAJO_FIN) f.bajo++;
  return f.bajo >= REG_BAJO_FIN;
}
