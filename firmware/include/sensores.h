// Drivers y validación de rangos (§7.1).
#pragma once
#include "tipos.h"
#include "config.h"

void sensores_iniciar();
// Llena todo menos masa_g y rpm; deja en `cuentas` las 8 lecturas brutas del HX711 (NAN si falla).
void sensores_leer(Lectura& l, float cuentas[HX_MUESTRAS]);
void sensores_simular_sht_cam(bool desconectado);   // prueba de falla
