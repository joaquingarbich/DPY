#include "boton.h"
#include "config.h"
#include "tipos.h"
#include <Arduino.h>

static QueueHandle_t q;
static bool estable_presionado = false, crudo_ant = false, larga_enviada = false;
static uint32_t t_cambio = 0, t_presion = 0;

void boton_iniciar(QueueHandle_t eventos) {
  q = eventos;
  pinMode(PIN_BOTON, INPUT_PULLUP);
}

static void enviar(Boton b) { xQueueSend(q, &b, 0); }

void boton_tick() {
  uint32_t ahora = millis();
  bool crudo = digitalRead(PIN_BOTON) == LOW;   // activo en bajo
  if (crudo != crudo_ant) { crudo_ant = crudo; t_cambio = ahora; }
  if (crudo != estable_presionado && ahora - t_cambio >= BOTON_ANTIRREBOTE_MS) {
    estable_presionado = crudo;
    if (crudo) { t_presion = ahora; larga_enviada = false; }
    else if (!larga_enviada && ahora - t_presion <= BOTON_CORTA_MAX_MS) enviar(Boton::CORTA);
  }
  if (estable_presionado && !larga_enviada && ahora - t_presion > BOTON_LARGA_MS) {
    larga_enviada = true;
    enviar(Boton::LARGA);
  }
}

bool boton_larga_en_curso() { return estable_presionado && larga_enviada; }
