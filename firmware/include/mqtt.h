// Cliente esp-mqtt: TLS, QoS 1, última voluntad y PUBACK (§11.2).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define TOPICO(s) "secador/" SECADOR_ID "/" s

struct EventoMqtt {
  enum : uint8_t { CONECTADO, DESCONECTADO, PUBACK, CONFIG } tipo;
  int id;          // msg_id del PUBACK
  uint16_t len;
  char datos[256]; // mensaje de configuración
};

void mqtt_iniciar(QueueHandle_t eventos);
// Devuelve el msg_id (> 0 con QoS 1) o -1 si no se pudo enviar.
int mqtt_publicar(const char* topico, const char* datos, bool retenido);
