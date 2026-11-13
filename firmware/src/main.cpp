// setup(): crea recursos y tareas (§5, §6). Aquí van también los cuerpos de las
// cuatro tareas, que unen los módulos.
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <sys/time.h>
#include "globales.h"
#include "config.h"
#include "almacenamiento.h"
#include "boton.h"
#include "celda.h"
#include "diagnostico.h"
#include "enlace.h"
#include "estados.h"
#include "filtros.h"
#include "fin_ciclo.h"
#include "mqtt.h"
#include "registro_json.h"
#include "reloj.h"
#include "sensores.h"
#include "ventilador.h"

SemaphoreHandle_t mtxLectura, mtxLote;
QueueHandle_t qRegistros, qBoton, qEventos;
TaskHandle_t hControl;
Lectura g_lectura{};
float g_t_cam_ctrl = NAN, g_cuentas_prom = NAN;
bool g_estable = false;
Lote g_lote{};
ParametrosLote g_param{X0_PROV, false, XF_DEF, SP_DEF, INTERVALO_DEF_S};
Calibracion g_cal;
volatile float g_duty = 0;
volatile bool g_estado_cambio = true, g_bloquear_control = false;

static QueueHandle_t qMqtt;
static Maquina maquina;
static volatile bool fin_pendiente = false;
static constexpr size_t LINEA = 400, LINEA_EV = 256;

// Lecturas del intervalo de registro (protegidas por mtxLectura)
static struct {
  float masa[MAX_LECT_INTERVALO], t_cam[MAX_LECT_INTERVALO], hr_cam[MAX_LECT_INTERVALO],
      t_ent[MAX_LECT_INTERVALO], hr_ent[MAX_LECT_INTERVALO], t_col[MAX_LECT_INTERVALO],
      luz[MAX_LECT_INTERVALO];
  int n;
} acum;

bool lote_activo(Estado e) {
  return e == Estado::OPERACION || e == Estado::FIN_CICLO || e == Estado::MODO_SEGURO;
}

void evento_con_lote(TipoEvento t, const char* lote, const char* datos) {
  char ts[21], linea[LINEA_EV];
  reloj_ts(ts);
  if (!evento_a_json(t, lote, ts, datos, linea, sizeof linea)) return;
  Serial.printf("[evento] %s\n", linea);
  xQueueSend(qEventos, linea, 0);  // cola llena: se pierde y queda en el monitor serie
}

void evento(TipoEvento t, const char* datos) {
  char id[sizeof g_lote.id];
  xSemaphoreTake(mtxLote, portMAX_DELAY);
  memcpy(id, g_lote.id, sizeof id);
  uint32_t seq = g_lote.seq;
  bool activo = lote_activo(g_lote.estado);
  xSemaphoreGive(mtxLote);
  if (activo) nvs_guardar_seq(seq);
  evento_con_lote(t, id, datos);
}

static void wdt_suscribir() {
  esp_task_wdt_add(nullptr);
}

// ---------------------------------------------------------------- tAdquisicion
static void tAdquisicion(void*) {
  wdt_suscribir();
  static const char* nombres[] = {"sht_cam", "sht_ent", "ds18b20", "bh1750", "hx711"};
  uint8_t nan_seguidos[5] = {};
  uint8_t rpm_bajas = 0;
  float cam_val[3] = {NAN, NAN, NAN};
  int cam_i = 0;
  TickType_t ultimo = xTaskGetTickCount();

  for (;;) {
    Lectura l{};
    float cuentas[HX_MUESTRAS];
    sensores_leer(l, cuentas);
    celda_agregar(cuentas, HX_MUESTRAS);
    float tara = 0;
    xSemaphoreTake(mtxLote, portMAX_DELAY);
    if (g_lote.estado != Estado::ESPERA) tara = g_lote.tara_cuentas;
    xSemaphoreGive(mtxLote);
    float c = mediana(cuentas, HX_MUESTRAS);
    if (isnan(c)) l.flags |= F_HX711;
    l.masa_g = celda_masa(c, tara, l.t_cam, g_cal.S, g_cal.CT);
    l.rpm = ventilador_rpm();

    for (int i = 0; i < 5; i++) {
      if (!(l.flags & (1 << i))) { nan_seguidos[i] = 0; continue; }
      if (++nan_seguidos[i] == NAN_ERROR) {
        char d[40];
        snprintf(d, sizeof d, "{\"sensor\":\"%s\"}", nombres[i]);
        evento(TipoEvento::ERROR_SENSOR, d);
      }
      if (nan_seguidos[i] > NAN_ERROR) nan_seguidos[i] = NAN_ERROR + 1;
    }
    if (g_duty >= DUTY_MIN && l.rpm < RPM_MIN) {
      if (++rpm_bajas == 3) {
        char d[40];
        snprintf(d, sizeof d, "{\"rpm\":%u,\"duty\":%.1f}", l.rpm, (float)g_duty);
        evento(TipoEvento::FALLA_VENTILADOR, d);
      }
      if (rpm_bajas > 3) rpm_bajas = 4;
    } else rpm_bajas = 0;

    if (!isnan(l.t_cam)) { cam_val[cam_i] = l.t_cam; cam_i = (cam_i + 1) % 3; }

    xSemaphoreTake(mtxLectura, portMAX_DELAY);
    g_lectura = l;
    g_t_cam_ctrl = mediana(cam_val, 3);
    g_estable = celda_estable(g_cal.S);
    g_cuentas_prom = celda_promedio(g_cal.S);
    if (acum.n < MAX_LECT_INTERVALO) {
      int k = acum.n++;
      acum.masa[k] = l.masa_g; acum.t_cam[k] = l.t_cam; acum.hr_cam[k] = l.hr_cam;
      acum.t_ent[k] = l.t_ent; acum.hr_ent[k] = l.hr_ent; acum.t_col[k] = l.t_col; acum.luz[k] = l.luz_lux;
    }
    xSemaphoreGive(mtxLectura);

    xTaskNotifyGive(hControl);
    // 60 s supera el plazo del watchdog (30 s): se espera en tramos de 5 s.
    for (uint32_t i = 0; i < T_ADQ_S / 5; i++) {
      esp_task_wdt_reset();
      vTaskDelayUntil(&ultimo, pdMS_TO_TICKS(5000));
    }
  }
}

// ---------------------------------------------------------------- tControl
// Número con un decimal o null (un "nan" dejaría el JSON inválido).
static const char* f1(char (&b)[16], float v) {
  if (isnan(v)) return "null";
  snprintf(b, sizeof b, "%.1f", v);
  return b;
}

// Efectos de cada evento de la máquina de estados (§9.1).
static void acciones(TipoEvento t, Estado previo, float masa, float cuentas) {
  char d[160] = "";
  char id[sizeof g_lote.id];
  char b1[16], b2[16];
  xSemaphoreTake(mtxLote, portMAX_DELAY);
  switch (t) {
    case TipoEvento::INICIO_LOTE: {
      uint16_t n = nvs_n_lote() + 1;
      nvs_guardar_n_lote(n);
      memset(&g_lote, 0, sizeof g_lote);
      snprintf(g_lote.id, sizeof g_lote.id, "L-%02u", n);
      g_lote.tara_cuentas = cuentas;
      g_lote.estado = Estado::CARGA;
      g_param.x0 = X0_PROV;
      g_param.x0_lab = false;
      g_param.xf = XF_DEF;
      nvs_guardar_lote(g_lote, g_param);
      snprintf(d, sizeof d, "{\"tara_cuentas\":%.0f}", cuentas);
      break;
    }
    case TipoEvento::MASA_INICIAL:
      g_lote.m0_g = masa;
      g_lote.seq = 0;
      g_lote.t_inicio = reloj_epoch();
      g_lote.m_obj_g = masa_objetivo(masa, g_param.x0, g_param.xf);
      nvs_guardar_lote(g_lote, g_param);
      snprintf(d, sizeof d, "{\"m0_g\":%.1f,\"m_obj_g\":%.1f,\"x0\":%.4f,\"xf\":%.3f}",
               masa, g_lote.m_obj_g, g_param.x0, g_param.xf);
      break;
    case TipoEvento::CARGA_CANCELADA:
      nvs_borrar_lote();
      break;
    case TipoEvento::CIERRE_MANUAL:
    case TipoEvento::CIERRE_LOTE:
      snprintf(d, sizeof d, "{\"motivo\":\"%s\",\"seq\":%lu,\"masa_g\":%s}",
               t == TipoEvento::CIERRE_MANUAL ? "manual" : "fin_ciclo", (unsigned long)g_lote.seq, f1(b1, masa));
      nvs_guardar_seq(g_lote.seq);
      nvs_borrar_lote();
      break;
    case TipoEvento::FIN_CICLO:
      nvs_guardar_lote(g_lote, g_param);
      snprintf(d, sizeof d, "{\"masa_g\":%s,\"m_obj_g\":%s}", f1(b1, masa), f1(b2, g_lote.m_obj_g));
      break;
    case TipoEvento::MODO_SEGURO:
      snprintf(d, sizeof d, "{\"previo\":\"%s\"}", nombre_estado(previo));
      nvs_guardar_seq(g_lote.seq);
      break;
    default:
      if (lote_activo(g_lote.estado)) nvs_guardar_seq(g_lote.seq);
      break;
  }
  memcpy(id, g_lote.id, sizeof id);
  bool cierra = t == TipoEvento::CARGA_CANCELADA || t == TipoEvento::CIERRE_MANUAL ||
                t == TipoEvento::CIERRE_LOTE;
  if (cierra) memset(&g_lote, 0, sizeof g_lote);  // estado = ESPERA
  xSemaphoreGive(mtxLote);
  evento_con_lote(t, id, d[0] ? d : nullptr);
}

static void tControl(void*) {
  wdt_suscribir();
  for (;;) {
    while (g_bloquear_control) vTaskDelay(pdMS_TO_TICKS(100));  // prueba wdt: sin alimentar
    bool nueva = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) > 0;
    Boton b = Boton::NINGUNO;
    xQueueReceive(qBoton, &b, 0);
    esp_task_wdt_reset();
    if (!nueva && b == Boton::NINGUNO) continue;

    EntradaControl e{};
    e.ahora_s = (uint32_t)(esp_timer_get_time() / 1000000);
    e.nueva_lectura = nueva;
    e.boton = b;
    xSemaphoreTake(mtxLectura, portMAX_DELAY);
    Lectura l = g_lectura;
    e.estable = g_estable;
    e.t_cam_ctrl = g_t_cam_ctrl;
    float cuentas = g_cuentas_prom;
    xSemaphoreGive(mtxLectura);
    xSemaphoreTake(mtxLote, portMAX_DELAY);
    float sp = g_param.setpoint;
    float tara = g_lote.tara_cuentas;
    xSemaphoreGive(mtxLote);

    // Para confirmar la carga (m0 y masa > 500 g) se usa el promedio estable de
    // 20 muestras con la tara del lote, no la última lectura.
    e.estable = e.estable && !isnan(cuentas);
    e.masa_g = celda_masa(cuentas, tara, l.t_cam, g_cal.S, g_cal.CT);
    e.cam_valida = !(l.flags & F_SHT_CAM);
    e.g_est = l.luz_lux / g_cal.K;
    e.dt_col = l.t_col - l.t_ent;
    e.fin_detectado = fin_pendiente;
    fin_pendiente = false;

    Estado antes = maquina.estado;
    SalidaControl s;
    maquina_paso(maquina, e, sp, s);
    ventilador_aplicar(s.ventilador, s.duty);
    g_duty = s.duty;

    xSemaphoreTake(mtxLote, portMAX_DELAY);
    g_lote.estado = maquina.estado;
    g_lote.pausa = maquina.pausa;
    xSemaphoreGive(mtxLote);
    // m0 sale del promedio estable; para los demás eventos basta la última lectura.
    for (int i = 0; i < s.n_ev; i++)
      acciones(s.ev[i], antes, s.ev[i] == TipoEvento::MASA_INICIAL ? e.masa_g : l.masa_g, cuentas);
    if (maquina.estado != antes || s.n_ev) g_estado_cambio = true;
  }
}

// ---------------------------------------------------------------- tRegistro
static void esperar_intervalo(uint16_t iv) {
  // Hasta el próximo múltiplo de `iv` en UTC; sin hora válida, `iv` segundos.
  uint32_t espera = iv * 1000UL;
  if (reloj_valido()) {
    timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t ms = tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
    espera = iv * 1000UL - ms % (iv * 1000ULL) + 20;
  }
  while (espera) {
    uint32_t d = espera > 5000 ? 5000 : espera;
    vTaskDelay(pdMS_TO_TICKS(d));
    espera -= d;
    esp_task_wdt_reset();
  }
}

static void tRegistro(void*) {
  wdt_suscribir();
  FinCiclo fin;
  bool sd_con_error = false;
  static char linea[LINEA];
  for (;;) {
    xSemaphoreTake(mtxLote, portMAX_DELAY);
    uint16_t iv = g_param.intervalo_s;
    xSemaphoreGive(mtxLote);
    esperar_intervalo(iv);

    Registro r{};
    reloj_ts(r.ts);
    xSemaphoreTake(mtxLectura, portMAX_DELAY);
    int n = acum.n;
    r.masa_g = masa_intervalo(acum.masa, n, DESCARTE_G, &r.n_descartes);
    r.t_cam = media(acum.t_cam, n);
    r.hr_cam = media(acum.hr_cam, n);
    r.t_ent = media(acum.t_ent, n);
    r.hr_ent = media(acum.hr_ent, n);
    r.t_col = media(acum.t_col, n);
    r.luz_lux = media(acum.luz, n);
    acum.n = 0;
    xSemaphoreGive(mtxLectura);
    r.duty = g_duty;
    r.senal_dbm = enlace().senal_dbm();

    xSemaphoreTake(mtxLote, portMAX_DELAY);
    bool activo = lote_activo(g_lote.estado);
    if (activo) {
      r.seq = ++g_lote.seq;
      r.estado = g_lote.estado;
      r.pausa = g_lote.pausa;
    }
    char id[sizeof g_lote.id];
    memcpy(id, g_lote.id, sizeof id);
    float m_obj = g_lote.m_obj_g;
    bool x0_lab = g_param.x0_lab;
    uint32_t ahora = reloj_epoch();
    uint32_t transcurrido = (ahora && g_lote.t_inicio) ? ahora - g_lote.t_inicio : r.seq * iv;
    if (activo && r.seq % 10 == 0) nvs_guardar_seq(r.seq);
    xSemaphoreGive(mtxLote);
    if (!activo) { esp_task_wdt_reset(); continue; }  // sin lote no se registra

    if (!registro_a_json(r, id, linea, sizeof linea)) continue;
    if (sd_agregar(id, linea)) sd_con_error = false;
    else {
      if (!sd_con_error) evento(TipoEvento::ERROR_SD);
      sd_con_error = true;
      xQueueSend(qRegistros, linea, 0);  // se envía directo
    }

    if (r.seq == 1) fin = FinCiclo{};
    if (r.estado == Estado::OPERACION && fin_ciclo_paso(fin, r.masa_g, m_obj, x0_lab, transcurrido))
      fin_pendiente = true;
    esp_task_wdt_reset();
  }
}

// ---------------------------------------------------------------- tComunicacion
struct Pendiente { int id; uint32_t off_fin, seq_sig; bool ack; };

static void aplicar_config(const EventoMqtt& m) {
  char d[160] = "";
  char id[sizeof g_lote.id];
  xSemaphoreTake(mtxLote, portMAX_DELAY);
  memcpy(id, g_lote.id, sizeof id);
  ResultadoConfig r = config_desde_json(m.datos, m.len, g_lote.id, g_param);
  if (r == ResultadoConfig::APLICADA) {
    if (g_lote.m0_g > 0) g_lote.m_obj_g = masa_objetivo(g_lote.m0_g, g_param.x0, g_param.xf);
    nvs_guardar_lote(g_lote, g_param);
    snprintf(d, sizeof d,
             "{\"x0\":%.4f,\"x0_lab\":%s,\"xf\":%.3f,\"setpoint\":%.1f,\"intervalo_s\":%u,\"m_obj_g\":%.1f}",
             g_param.x0, g_param.x0_lab ? "true" : "false", g_param.xf, g_param.setpoint,
             g_param.intervalo_s, g_lote.m_obj_g);
  }
  xSemaphoreGive(mtxLote);
  if (r == ResultadoConfig::APLICADA) evento_con_lote(TipoEvento::CONFIG_APLICADA, id, d);
  if (r == ResultadoConfig::RECHAZADA) evento_con_lote(TipoEvento::CONFIG_RECHAZADA, id, nullptr);
}

static void publicar_estado() {
  char d[128];
  xSemaphoreTake(mtxLote, portMAX_DELAY);
  snprintf(d, sizeof d, "{\"en_linea\":true,\"fw\":\"%s\",\"estado\":\"%s\",\"pausa\":%s,\"lote\":%s%s%s}",
           FW_VERSION, nombre_estado(g_lote.estado), g_lote.pausa ? "true" : "false",
           g_lote.id[0] ? "\"" : "", g_lote.id[0] ? g_lote.id : "null", g_lote.id[0] ? "\"" : "");
  xSemaphoreGive(mtxLote);
  if (mqtt_publicar(TOPICO("estado"), d, true) > 0) g_estado_cambio = false;
}

static void tComunicacion(void*) {
  wdt_suscribir();
  enlace().iniciar();
  mqtt_iniciar(qMqtt);
  static Pendiente ven[VENTANA_MQTT];
  static char lote_envio[sizeof g_lote.id] = "";
  static char buf[LINEA + 112];
  static char ev[LINEA_EV];
  static EventoMqtt m;
  int nv = 0;
  uint32_t off_envio = 0, t_pub = 0;
  bool conectado = false;

  for (;;) {
    enlace().mantener();
    if (enlace().conectado()) reloj_iniciar_ntp();
    reloj_mantener();

    // Lote cuyo archivo se está enviando: se termina uno antes de pasar al siguiente.
    // ponytail: tras un reinicio solo se reenvían pendientes del lote activo; los de un
    // lote ya cerrado quedan en la microSD (exportarlos a mano si hiciera falta).
    if (!lote_envio[0]) {
      xSemaphoreTake(mtxLote, portMAX_DELAY);
      if (lote_activo(g_lote.estado)) memcpy(lote_envio, g_lote.id, sizeof lote_envio);
      xSemaphoreGive(mtxLote);
      if (lote_envio[0]) { uint32_t s; sd_leer_ptr(lote_envio, off_envio, s); nv = 0; }
    }

    while (xQueueReceive(qMqtt, &m, conectado ? 0 : pdMS_TO_TICKS(50)) == pdTRUE) {
      switch (m.tipo) {
        case EventoMqtt::CONECTADO: {
          conectado = true;
          g_estado_cambio = true;
          nv = 0;
          uint32_t s;
          if (lote_envio[0]) sd_leer_ptr(lote_envio, off_envio, s);
          break;
        }
        case EventoMqtt::DESCONECTADO:
          conectado = false;
          nv = 0;
          break;
        case EventoMqtt::PUBACK: {
          for (int i = 0; i < nv; i++) if (ven[i].id == m.id) ven[i].ack = true;
          int k = 0;
          while (k < nv && ven[k].ack) k++;
          if (k) {
            sd_escribir_ptr(lote_envio, ven[k - 1].off_fin, ven[k - 1].seq_sig);
            memmove(ven, ven + k, (nv - k) * sizeof ven[0]);
            nv -= k;
          }
          break;
        }
        case EventoMqtt::CONFIG:
          aplicar_config(m);
          break;
      }
    }

    if (conectado && millis() - t_pub >= PUB_ESPERA_MS) {
      if (g_estado_cambio) { publicar_estado(); t_pub = millis(); }
      else if (xQueuePeek(qEventos, ev, 0) == pdTRUE) {
        if (mqtt_publicar(TOPICO("evento"), ev, false) > 0) xQueueReceive(qEventos, ev, 0);
        t_pub = millis();
      } else if (xQueuePeek(qRegistros, buf, 0) == pdTRUE) {   // microSD con falla
        if (mqtt_publicar(TOPICO("telemetria"), buf, false) > 0) xQueueReceive(qRegistros, buf, 0);
        t_pub = millis();
      } else if (lote_envio[0] && nv < VENTANA_MQTT) {
        uint32_t off_sig;
        size_t len = sd_leer_linea(lote_envio, off_envio, buf, sizeof buf, off_sig);
        if (len) {
          int id = mqtt_publicar(TOPICO("telemetria"), buf, false);
          if (id > 0) {
            ven[nv++] = {id, off_sig, seq_de_linea(buf) + 1, false};
            off_envio = off_sig;
          }
          t_pub = millis();
        } else if (nv == 0) {
          xSemaphoreTake(mtxLote, portMAX_DELAY);
          bool sigue = lote_activo(g_lote.estado) && !strcmp(g_lote.id, lote_envio);
          xSemaphoreGive(mtxLote);
          if (!sigue) lote_envio[0] = '\0';   // lote cerrado y todo confirmado
        }
      }
    }
    if (!conectado) vTaskDelay(pdMS_TO_TICKS(50));
    else vTaskDelay(pdMS_TO_TICKS(10));
    esp_task_wdt_reset();
  }
}

// ---------------------------------------------------------------- setup
static const char* motivo_reinicio() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "encendido";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panico";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "baja_tension";
    case ESP_RST_EXT:      return "externo";
    default:               return "otro";
  }
}

static void tick_10ms(void*) {
  boton_tick();
  led_tick();
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\nsecador %s fw %s\n", SECADOR_ID, FW_VERSION);

  mtxLectura = xSemaphoreCreateMutex();
  mtxLote = xSemaphoreCreateMutex();
  qRegistros = xQueueCreate(32, LINEA);
  qBoton = xQueueCreate(8, sizeof(Boton));
  qEventos = xQueueCreate(32, LINEA_EV);
  qMqtt = xQueueCreate(16, sizeof(EventoMqtt));

  nvs_iniciar();
  g_cal = nvs_cargar_cal();
  reloj_iniciar();
  sensores_iniciar();
  ventilador_iniciar();
  led_iniciar();
  if (!sd_iniciar()) evento(TipoEvento::ERROR_SD);

  if (nvs_cargar_lote(g_lote, g_param)) {
    uint32_t s = sd_ultimo_seq(g_lote.id);     // el archivo es más reciente que la NVS
    if (s > g_lote.seq) g_lote.seq = s;
    g_lote.m_obj_g = masa_objetivo(g_lote.m0_g, g_param.x0, g_param.xf);
    maquina.estado = g_lote.estado;
    char d[64];
    snprintf(d, sizeof d, "{\"motivo\":\"%s\",\"estado\":\"%s\"}", motivo_reinicio(), nombre_estado(g_lote.estado));
    evento(TipoEvento::REANUDACION, d);
  } else {
    nvs_borrar_lote();   // un lote en CARGA no se retoma
    memset(&g_lote, 0, sizeof g_lote);
  }

  esp_task_wdt_config_t wdt = {.timeout_ms = WDT_MS, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);

  boton_iniciar(qBoton);
  esp_timer_create_args_t ta = {};
  ta.callback = tick_10ms;
  ta.name = "boton_led";
  esp_timer_handle_t th;
  esp_timer_create(&ta, &th);
  esp_timer_start_periodic(th, 10000);

  xTaskCreatePinnedToCore(tControl, "tControl", 4096, nullptr, 4, &hControl, 1);
  xTaskCreatePinnedToCore(tAdquisicion, "tAdquisicion", 4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(tRegistro, "tRegistro", 6144, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(tComunicacion, "tComunicacion", 8192, nullptr, 2, nullptr, 0);
}

void loop() {
  consola_atender();
  delay(20);
}
