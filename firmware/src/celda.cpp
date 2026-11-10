#include "celda.h"
#include "config.h"
#include "filtros.h"
#include <math.h>

static float anillo[TARA_N];
static int pos = 0, llenas = 0;

void celda_agregar(const float* c, int n) {
  for (int i = 0; i < n; i++) {
    if (isnan(c[i])) continue;
    anillo[pos] = c[i];
    pos = (pos + 1) % TARA_N;
    if (llenas < TARA_N) llenas++;
  }
}

float celda_masa(float cuentas, float tara, float t_cam, float S, float CT) {
  if (isnan(cuentas)) return NAN;
  float corr = isnan(t_cam) ? 0 : CT * (t_cam - 25);  // sin T de cámara, sin corrección
  float m = (cuentas - tara) / S + corr;
  return (m < MASA_MIN_G || m > MASA_MAX_G) ? NAN : m;
}

bool celda_estable(float S) {
  if (llenas < ESTABLE_N) return false;
  float ult[ESTABLE_N];
  for (int i = 0; i < ESTABLE_N; i++) ult[i] = anillo[(pos - 1 - i + TARA_N) % TARA_N];
  return desviacion(ult, ESTABLE_N) / S < ESTABLE_DS_G;
}

// Solo si las 20 también están quietas: así no se promedian muestras de antes de cargar.
float celda_promedio(float S) {
  if (llenas < TARA_N || desviacion(anillo, TARA_N) / S >= ESTABLE_DS_G) return NAN;
  return media(anillo, TARA_N);
}
