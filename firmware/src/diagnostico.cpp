#include "diagnostico.h"
#include "globales.h"
#include "boton.h"
#include "config.h"
#include "enlace.h"
#include "registro_json.h"
#include "sensores.h"

void led_iniciar() { pinMode(PIN_LED, OUTPUT); }

void led_tick() {
  uint32_t t = millis();
  bool on = false;
  if (boton_larga_en_curso()) on = true;
  else switch (g_lote.estado) {
    case Estado::ESPERA:      on = false; break;
    case Estado::CARGA:       on = t % 1000 < 500; break;                       // 1 Hz
    case Estado::OPERACION:
      on = g_lote.pausa ? (t % 5000 < 100 || (t % 5000 >= 250 && t % 5000 < 350))  // doble destello
                        : t % 5000 < 100;                                          // destello
      break;
    case Estado::FIN_CICLO:   on = t % 250 < 125; break;                        // 4 Hz
    case Estado::MODO_SEGURO: on = true; break;
  }
  digitalWrite(PIN_LED, on);
}

static float cal_cero = NAN;

static void imprimir_estado() {
  Lectura l;
  float prom;
  bool est;
  xSemaphoreTake(mtxLectura, portMAX_DELAY);
  l = g_lectura; prom = g_cuentas_prom; est = g_estable;
  xSemaphoreGive(mtxLectura);
  xSemaphoreTake(mtxLote, portMAX_DELAY);
  Lote lo = g_lote;
  ParametrosLote p = g_param;
  xSemaphoreGive(mtxLote);
  Serial.printf("fw %s  eq %s  estado %s%s  lote %s  seq %lu\n", FW_VERSION, SECADOR_ID,
                nombre_estado(lo.estado), lo.pausa ? " (pausa)" : "", lo.id[0] ? lo.id : "-",
                (unsigned long)lo.seq);
  Serial.printf("m0 %.1f g  m_obj %.1f g  x0 %.4f (%s)  xf %.3f  sp %.1f C  intervalo %u s\n",
                lo.m0_g, lo.m_obj_g, p.x0, p.x0_lab ? "lab" : "provisional", p.xf, p.setpoint,
                p.intervalo_s);
  Serial.printf("masa %.1f g (%s)  cuentas %.0f  t_cam %.2f  hr_cam %.1f  t_ent %.2f  hr_ent %.1f\n",
                l.masa_g, est ? "estable" : "inestable", prom, l.t_cam, l.hr_cam, l.t_ent, l.hr_ent);
  Serial.printf("t_col %.2f  luz %.0f lx  rpm %u  duty %.1f %%  flags 0x%02x  senal %d dBm\n",
                l.t_col, l.luz_lux, l.rpm, g_duty, l.flags, enlace().senal_dbm());
  Serial.printf("cal S %.3f  CT %.3f  K %.2f  n_lote %u\n", g_cal.S, g_cal.CT, g_cal.K, nvs_n_lote());
}

static bool cuentas_estables(float& c) {
  xSemaphoreTake(mtxLectura, portMAX_DELAY);
  c = g_cuentas_prom;
  bool est = g_estable;
  xSemaphoreGive(mtxLectura);
  if (!est || isnan(c)) Serial.println("lectura inestable: espere y repita");
  return est && !isnan(c);
}

static void ejecutar(const char* s) {
  float v;
  unsigned n;
  if (!strcmp(s, "estado")) imprimir_estado();
  else if (!strcmp(s, "cal tara")) {
    if (cuentas_estables(cal_cero)) Serial.printf("cero = %.0f cuentas. Ponga la masa patron y use: cal S <masa_g>\n", cal_cero);
  } else if (sscanf(s, "cal S %f", &v) == 1) {
    float c;
    if (isnan(cal_cero)) Serial.println("primero: cal tara");
    else if (v <= 0) Serial.println("masa invalida");
    else if (cuentas_estables(c)) {
      g_cal.S = (c - cal_cero) / v;
      nvs_guardar_cal("cal_S", g_cal.S);
      Serial.printf("S = %.3f cuentas/g guardado\n", g_cal.S);
    }
  } else if (sscanf(s, "cal CT %f", &v) == 1) {
    g_cal.CT = v; nvs_guardar_cal("cal_CT", v); Serial.printf("CT = %.4f g/C guardado\n", v);
  } else if (sscanf(s, "cal K %f", &v) == 1 && v > 0) {
    g_cal.K = v; nvs_guardar_cal("cal_K", v); Serial.printf("K = %.3f lx/(W/m2) guardado\n", v);
  } else if (sscanf(s, "lote n %u", &n) == 1) {
    if (g_lote.estado != Estado::ESPERA) Serial.println("solo en ESPERA");
    else { nvs_guardar_n_lote(n); Serial.printf("el proximo lote sera L-%02u\n", n + 1); }
  }
#ifdef MODO_PRUEBAS
  else if (!strcmp(s, "prueba wdt")) { Serial.println("bloqueando tControl..."); g_bloquear_control = true; }
  else if (!strcmp(s, "prueba sht_cam off")) sensores_simular_sht_cam(true);
  else if (!strcmp(s, "prueba sht_cam on")) sensores_simular_sht_cam(false);
  else if (sscanf(s, "prueba red off %u", &n) == 1) enlace().suspender(n);
#endif
  else Serial.println("comandos: estado | cal tara | cal S <g> | cal CT <v> | cal K <v> | lote n <num>"
#ifdef MODO_PRUEBAS
                      " | prueba wdt | prueba sht_cam off|on | prueba red off <min>"
#endif
                      );
}

void consola_atender() {
  static char linea[64];
  static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { linea[n] = '\0'; if (n) ejecutar(linea); n = 0; }
    else if (n < (int)sizeof linea - 1) linea[n++] = c;
  }
}
