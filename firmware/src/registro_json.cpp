#include "registro_json.h"
#include "config.h"
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

const char* nombre_estado(Estado e) {
  static const char* n[] = {"espera", "carga", "operacion", "fin_ciclo", "modo_seguro"};
  return n[(int)e];
}

const char* nombre_evento(TipoEvento t) {
  static const char* n[] = {
      "inicio_lote", "masa_inicial", "carga_cancelada", "cierre_lote", "cierre_lote",
      "fin_ciclo", "modo_seguro", "fin_modo_seguro", "pausa_inicio", "pausa_fin",
      "error_sensor", "falla_ventilador", "error_sd", "config_aplicada", "config_rechazada",
      "reanudacion"};
  return n[(int)t];
}

// Agrega ,"k":valor (o null) con `dec` decimales.
static int num(char* p, size_t n, const char* k, float v, int dec) {
  if (isnan(v)) return snprintf(p, n, ",\"%s\":null", k);
  return snprintf(p, n, ",\"%s\":%.*f", k, dec, v);
}

static int texto_o_null(char* p, size_t n, const char* k, const char* v) {
  if (!v || !v[0]) return snprintf(p, n, "\"%s\":null", k);
  return snprintf(p, n, "\"%s\":\"%s\"", k, v);
}

size_t registro_a_json(const Registro& r, const char* lote, char* buf, size_t n) {
  size_t k = snprintf(buf, n, "{\"eq\":\"%s\",\"fw\":\"%s\",\"lote\":\"%s\",\"seq\":%lu,",
                      SECADOR_ID, FW_VERSION, lote, (unsigned long)r.seq);
  if (k >= n) return 0;
  k += texto_o_null(buf + k, n - k, "ts", r.ts);                 if (k >= n) return 0;
  k += num(buf + k, n - k, "masa_g", r.masa_g, 1);               if (k >= n) return 0;
  k += num(buf + k, n - k, "t_cam", r.t_cam, 2);                 if (k >= n) return 0;
  k += num(buf + k, n - k, "hr_cam", r.hr_cam, 1);               if (k >= n) return 0;
  k += num(buf + k, n - k, "t_ent", r.t_ent, 2);                 if (k >= n) return 0;
  k += num(buf + k, n - k, "hr_ent", r.hr_ent, 1);               if (k >= n) return 0;
  k += num(buf + k, n - k, "t_col", r.t_col, 2);                 if (k >= n) return 0;
  k += num(buf + k, n - k, "luz_lux", r.luz_lux, 0);             if (k >= n) return 0;
  k += num(buf + k, n - k, "duty", r.duty, 1);                   if (k >= n) return 0;
  k += snprintf(buf + k, n - k, ",\"estado\":\"%s\",\"pausa\":%s,\"n_descartes\":%u",
                nombre_estado(r.estado), r.pausa ? "true" : "false", r.n_descartes);
  if (k >= n) return 0;
  k += num(buf + k, n - k, "senal_dbm", r.senal_dbm == SIN_SENAL ? NAN : r.senal_dbm, 0);
  if (k >= n) return 0;
  k += snprintf(buf + k, n - k, "}");
  return k < n ? k : 0;
}

size_t evento_a_json(TipoEvento t, const char* lote, const char* ts, const char* datos, char* buf, size_t n) {
  bool critico = t == TipoEvento::FIN_CICLO || t == TipoEvento::MODO_SEGURO;
  size_t k = snprintf(buf, n, "{\"eq\":\"%s\",", SECADOR_ID);
  if (k >= n) return 0;
  k += texto_o_null(buf + k, n - k, "lote", lote);  if (k >= n) return 0;
  k += snprintf(buf + k, n - k, ",");               if (k >= n) return 0;
  k += texto_o_null(buf + k, n - k, "ts", ts);      if (k >= n) return 0;
  k += snprintf(buf + k, n - k, ",\"tipo\":\"%s\",\"nivel\":\"%s\",\"datos\":%s}",
                nombre_evento(t), critico ? "critico" : "info", datos ? datos : "{}");
  return k < n ? k : 0;
}

ResultadoConfig config_desde_json(const char* json, size_t len, const char* lote_activo, ParametrosLote& p) {
  JsonDocument d;
  if (deserializeJson(d, json, len)) return ResultadoConfig::RECHAZADA;
  const char* lote = d["lote"];
  if (!lote || !lote_activo || !lote_activo[0] || strcmp(lote, lote_activo) != 0)
    return ResultadoConfig::IGNORADA;

  ParametrosLote q = p;
  if (!d["x0"].isNull()) { q.x0 = d["x0"]; q.x0_lab = d["x0_lab"] | false; }
  if (!d["xf"].isNull()) q.xf = d["xf"];
  if (!d["setpoint"].isNull()) q.setpoint = d["setpoint"];
  long iv = d["intervalo_s"].isNull() ? (long)q.intervalo_s : d["intervalo_s"].as<long>();

  bool ok = q.x0 >= 0.70f && q.x0 <= 0.90f && q.xf >= 0.10f && q.xf <= 0.25f &&
            q.setpoint >= 45 && q.setpoint <= 65 && iv >= INTERVALO_MIN_S && iv <= INTERVALO_MAX_S;
  if (!ok) return ResultadoConfig::RECHAZADA;
  q.intervalo_s = (uint16_t)iv;
  if (memcmp(&q, &p, sizeof q) == 0) return ResultadoConfig::SIN_CAMBIOS;
  p = q;
  return ResultadoConfig::APLICADA;
}
