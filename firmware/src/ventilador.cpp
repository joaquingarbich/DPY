#include "ventilador.h"
#include "config.h"
#include <Arduino.h>

static volatile uint32_t pulsos = 0;
static uint32_t t_ant = 0;

static void IRAM_ATTR isr_taco() { pulsos = pulsos + 1; }

void ventilador_iniciar() {
  pinMode(PIN_VENT_ON, OUTPUT);
  digitalWrite(PIN_VENT_ON, LOW);
  ledcAttachChannel(PIN_PWM, PWM_HZ, PWM_BITS, PWM_CANAL);
  ledcWrite(PIN_PWM, 0);
  pinMode(PIN_TACO, INPUT);  // GPIO 35 no tiene pull-up interno: 10 kΩ externa
  attachInterrupt(PIN_TACO, isr_taco, FALLING);
  t_ant = millis();
}

void ventilador_aplicar(bool encendido, float duty) {
  if (!encendido) duty = 0;
  ledcWrite(PIN_PWM, (uint32_t)(duty / 100.0f * ((1 << PWM_BITS) - 1) + 0.5f));
  digitalWrite(PIN_VENT_ON, encendido ? HIGH : LOW);
}

uint16_t ventilador_rpm() {
  uint32_t ahora = millis();
  noInterrupts();
  uint32_t p = pulsos;
  pulsos = 0;
  interrupts();
  uint32_t dt = ahora - t_ant;
  t_ant = ahora;
  return dt ? (uint16_t)(p * 60000UL / PULSOS_VUELTA / dt) : 0;
}
