// Mediana y media que ignoran NAN (§7.2).
#pragma once
#include <stdint.h>

float mediana(const float* v, int n);   // NAN si no hay valores válidos
float media(const float* v, int n);     // NAN si no hay valores válidos
float desviacion(const float* v, int n);
// Masa del intervalo: mediana de las lecturas que no se alejan más de `tol`
// de la mediana. Cuenta en *n_desc los NAN y los descartados.
float masa_intervalo(const float* v, int n, float tol, uint16_t* n_desc);
