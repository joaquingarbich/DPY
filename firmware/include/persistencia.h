// NVS, espacio "secador" (§12).
#pragma once
#include "tipos.h"

struct Calibracion { float S, CT, K; };

void nvs_iniciar();
Calibracion nvs_cargar_cal();                 // valores de config.h si no hay
void nvs_guardar_cal(const char* clave, float v);   // "cal_S", "cal_CT", "cal_K"
// true si hay un lote en OPERACION, FIN_CICLO o MODO_SEGURO
bool nvs_cargar_lote(Lote& l, ParametrosLote& p);
void nvs_guardar_lote(const Lote& l, const ParametrosLote& p);
void nvs_guardar_seq(uint32_t seq);
void nvs_borrar_lote();
uint16_t nvs_n_lote();
void nvs_guardar_n_lote(uint16_t n);
