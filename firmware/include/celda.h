// Celda de carga: tara, factor de escala, corrección térmica y estabilidad (§7.3).
#pragma once

void celda_agregar(const float* cuentas, int n);   // guarda las últimas 20 muestras brutas
float celda_masa(float cuentas, float tara, float t_cam, float S, float CT);  // NAN fuera de rango
bool celda_estable(float S);                       // desv. estándar de las últimas 10 < 2 g
float celda_promedio(float S);                     // promedio de las últimas 20 si están estables; si no, NAN
