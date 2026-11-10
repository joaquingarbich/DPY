// Masa objetivo y detección de fin de ciclo (§9.2).
#pragma once
#include <stdint.h>

float masa_objetivo(float m0, float x0, float xf);

struct FinCiclo { uint8_t bajo = 0; };

// Se llama con cada registro en OPERACION. Devuelve true al tercer registro
// seguido bajo m_obj. Con x0 provisional queda inhibido las primeras 12 h.
bool fin_ciclo_paso(FinCiclo& f, float masa_g, float m_obj_g, bool x0_lab, uint32_t s_desde_inicio);
