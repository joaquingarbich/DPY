// Capa de enlace (§11.1): el resto del firmware solo usa esta interfaz,
// para poder cambiar WiFi por un módulo celular por UART2.
#pragma once
#include <stdint.h>

class Enlace {
 public:
  virtual bool iniciar() = 0;
  virtual bool conectado() = 0;
  virtual int8_t senal_dbm() = 0;
  virtual void mantener() = 0;   // reconexión con espera exponencial: 5, 10, 20, 40, 60 s
  virtual void suspender(uint32_t minutos) = 0;   // prueba "red off"
  virtual ~Enlace() {}
};

class EnlaceWiFi : public Enlace {
 public:
  bool iniciar() override;
  bool conectado() override;
  int8_t senal_dbm() override;
  void mantener() override;
  void suspender(uint32_t minutos) override;
 private:
  uint32_t proximo_ms = 0, suspendido_hasta = 0;
  uint8_t intento = 0;
  bool suspendido = false;
};
// class EnlaceCelular : public Enlace { /* AT por UART2 (GPIO 16/17); mismo contrato */ };

Enlace& enlace();
