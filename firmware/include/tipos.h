// Estructuras compartidas (documentación §5). Sin dependencias de Arduino
// para que compilen en el entorno native de pruebas.
#pragma once
#include <stdint.h>

enum class Estado : uint8_t { ESPERA, CARGA, OPERACION, FIN_CICLO, MODO_SEGURO };

// Bits de Lectura::flags
enum : uint8_t {
  F_SHT_CAM = 1 << 0, F_SHT_ENT = 1 << 1, F_DS18B20 = 1 << 2,
  F_BH1750 = 1 << 3, F_HX711 = 1 << 4, F_LUZ_SAT = 1 << 5,
};

struct Lectura {            // resultado de un ciclo de adquisición (60 s)
  float t_cam, hr_cam;      // SHT31 0x45
  float t_ent, hr_ent;      // SHT31 0x44
  float t_col;              // DS18B20
  float luz_lux;            // BH1750
  float masa_g;             // HX711 corregida (producto, sin tara)
  uint16_t rpm;             // un solo tacómetro (GPIO 35)
  uint8_t flags;            // bit a bit: sensor con error en esta lectura
  uint32_t t_ms;
};

struct ParametrosLote {     // llegan por secador/config; se guardan en NVS
  float x0;                 // humedad inicial b.h. (fracción)
  bool  x0_lab;             // true cuando x0 viene de la gravimetría
  float xf;                 // humedad final objetivo b.h. (fracción)
  float setpoint;           // °C
  uint16_t intervalo_s;     // 60 a 1800
};

struct Lote {
  char id[12];              // "L-02"
  float tara_cuentas;       // lectura bruta de la bandeja vacía
  float m0_g;               // masa inicial confirmada
  float m_obj_g;            // calculada con x0 (provisional o de laboratorio)
  uint32_t seq;             // número del último registro
  uint32_t t_inicio;        // época UTC de masa_inicial (para la inhibición de 12 h)
  Estado estado;
  bool pausa;
};

struct Registro {           // consolidado del intervalo (lo que se guarda y se envía)
  char ts[21];              // "2026-10-01T19:05:00Z"; vacío = null
  uint32_t seq;
  float masa_g, t_cam, hr_cam, t_ent, hr_ent, t_col, luz_lux, duty;
  Estado estado; bool pausa;
  uint16_t n_descartes;
  int8_t senal_dbm;         // SIN_SENAL = sin enlace → null
};
constexpr int8_t SIN_SENAL = INT8_MIN;

enum class TipoEvento : uint8_t {
  INICIO_LOTE, MASA_INICIAL, CARGA_CANCELADA, CIERRE_MANUAL, CIERRE_LOTE,
  FIN_CICLO, MODO_SEGURO, FIN_MODO_SEGURO, PAUSA_INICIO, PAUSA_FIN,
  ERROR_SENSOR, FALLA_VENTILADOR, ERROR_SD, CONFIG_APLICADA, CONFIG_RECHAZADA,
  REANUDACION,
};

enum class Boton : uint8_t { NINGUNO, CORTA, LARGA };
