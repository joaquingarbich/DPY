#include "mqtt.h"
#include "config.h"
#include "secretos.h"
#include <mqtt_client.h>
#include <string.h>

static esp_mqtt_client_handle_t cli;
static QueueHandle_t q;

static void manejador(void*, esp_event_base_t, int32_t id, void* datos) {
  auto ev = (esp_mqtt_event_handle_t)datos;
  EventoMqtt m{};
  switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
      esp_mqtt_client_subscribe(cli, TOPICO("config"), 1);
      m.tipo = EventoMqtt::CONECTADO;
      break;
    case MQTT_EVENT_DISCONNECTED:
      m.tipo = EventoMqtt::DESCONECTADO;
      break;
    case MQTT_EVENT_PUBLISHED:
      m.tipo = EventoMqtt::PUBACK;
      m.id = ev->msg_id;
      break;
    case MQTT_EVENT_DATA:
      // config es pequeño: se descarta si llega fragmentado o no cabe
      if (ev->current_data_offset != 0 || ev->data_len != ev->total_data_len ||
          ev->data_len >= (int)sizeof m.datos) return;
      m.tipo = EventoMqtt::CONFIG;
      m.len = ev->data_len;
      memcpy(m.datos, ev->data, ev->data_len);
      break;
    default:
      return;
  }
  xQueueSend(q, &m, 0);
}

void mqtt_iniciar(QueueHandle_t eventos) {
  q = eventos;
  esp_mqtt_client_config_t c = {};
  c.broker.address.uri = "mqtts://" MQTT_HOST ":8883";
  c.broker.verification.certificate = ROOT_CA;
  c.credentials.username = MQTT_USUARIO;
  c.credentials.client_id = SECADOR_ID;
  c.credentials.authentication.password = MQTT_CLAVE;
  c.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
  c.session.keepalive = 60;
  c.session.disable_clean_session = true;
  c.session.last_will.topic = TOPICO("estado");
  c.session.last_will.msg = "{\"en_linea\":false}";
  c.session.last_will.qos = 1;
  c.session.last_will.retain = 1;
  cli = esp_mqtt_client_init(&c);
  esp_mqtt_client_register_event(cli, MQTT_EVENT_ANY, manejador, nullptr);
  esp_mqtt_client_start(cli);
}

int mqtt_publicar(const char* topico, const char* datos, bool retenido) {
  return esp_mqtt_client_publish(cli, topico, datos, 0, 1, retenido);
}
