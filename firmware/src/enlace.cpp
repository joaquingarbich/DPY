#include "enlace.h"
#include "secretos.h"
#include <Arduino.h>
#include <WiFi.h>

static const uint16_t ESPERAS_S[] = {5, 10, 20, 40, 60};

bool EnlaceWiFi::iniciar() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);   // la reconexión la maneja mantener()
  WiFi.setSleep(true);            // modem-sleep
  WiFi.begin(WIFI_SSID, WIFI_CLAVE);
  proximo_ms = millis() + ESPERAS_S[0] * 1000;
  return true;
}

bool EnlaceWiFi::conectado() { return !suspendido && WiFi.status() == WL_CONNECTED; }

int8_t EnlaceWiFi::senal_dbm() { return conectado() ? (int8_t)WiFi.RSSI() : INT8_MIN; }

void EnlaceWiFi::mantener() {
  uint32_t ahora = millis();
  if (suspendido) {
    if ((int32_t)(ahora - suspendido_hasta) < 0) return;
    suspendido = false;
    intento = 0;
    WiFi.begin(WIFI_SSID, WIFI_CLAVE);
    proximo_ms = ahora + ESPERAS_S[0] * 1000;
    return;
  }
  if (WiFi.status() == WL_CONNECTED) { intento = 0; return; }
  if ((int32_t)(ahora - proximo_ms) < 0) return;
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_CLAVE);
  if (intento < 4) intento++;
  proximo_ms = ahora + ESPERAS_S[intento] * 1000;
}

void EnlaceWiFi::suspender(uint32_t minutos) {
  suspendido = true;
  suspendido_hasta = millis() + minutos * 60000UL;
  WiFi.disconnect();
}

Enlace& enlace() {
  static EnlaceWiFi wifi;
  return wifi;
}
