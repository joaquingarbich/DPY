// Recursos compartidos entre tareas (§5). Se definen en main.cpp.
// Orden de bloqueo cuando hacen falta los dos: mtxLectura y luego mtxLote.
#pragma once
#include <Arduino.h>
#include "tipos.h"
#include "persistencia.h"

extern SemaphoreHandle_t mtxLectura;  // g_lectura, g_t_cam_ctrl, g_estable, g_cuentas_prom
extern SemaphoreHandle_t mtxLote;     // g_lote, g_param
extern QueueHandle_t qRegistros;      // líneas JSON a enviar directo si falla la microSD
extern QueueHandle_t qBoton;          // Boton
extern QueueHandle_t qEventos;        // líneas JSON de eventos
extern TaskHandle_t hControl;

extern Lectura g_lectura;
extern float g_t_cam_ctrl, g_cuentas_prom;
extern bool g_estable;
extern Lote g_lote;
extern ParametrosLote g_param;
extern Calibracion g_cal;
extern volatile float g_duty;
extern volatile bool g_estado_cambio;     // publicar secador/<eq>/estado
extern volatile bool g_bloquear_control;  // prueba wdt

bool lote_activo(Estado e);   // OPERACION, FIN_CICLO o MODO_SEGURO
// Arma el evento con el lote activo (toma mtxLote) y lo encola.
void evento(TipoEvento t, const char* datos = nullptr);
void evento_con_lote(TipoEvento t, const char* lote, const char* datos);
