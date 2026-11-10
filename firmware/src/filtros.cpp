#include "filtros.h"
#include <math.h>
#include <algorithm>

static constexpr int MAX_N = 32;

float mediana(const float* v, int n) {
  float a[MAX_N];
  int k = 0;
  for (int i = 0; i < n && k < MAX_N; i++)
    if (!isnan(v[i])) a[k++] = v[i];
  if (k == 0) return NAN;
  std::sort(a, a + k);
  return (k % 2) ? a[k / 2] : (a[k / 2 - 1] + a[k / 2]) / 2;
}

float media(const float* v, int n) {
  float s = 0;
  int k = 0;
  for (int i = 0; i < n; i++)
    if (!isnan(v[i])) { s += v[i]; k++; }
  return k ? s / k : NAN;
}

float desviacion(const float* v, int n) {
  float m = media(v, n);
  if (isnan(m)) return NAN;
  float s = 0;
  int k = 0;
  for (int i = 0; i < n; i++)
    if (!isnan(v[i])) { s += (v[i] - m) * (v[i] - m); k++; }
  return sqrtf(s / k);
}

float masa_intervalo(const float* v, int n, float tol, uint16_t* n_desc) {
  float med = mediana(v, n);
  float a[MAX_N];
  int k = 0;
  uint16_t d = 0;
  for (int i = 0; i < n && i < MAX_N; i++) {
    if (isnan(v[i]) || fabsf(v[i] - med) > tol) d++;
    else a[k++] = v[i];
  }
  if (n_desc) *n_desc = d;
  return mediana(a, k);
}
