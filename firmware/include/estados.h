// Máquina de estados, pausa y prioridad sobre el ventilador (§8.2, §8.3, §9.1).
// Sin hardware: recibe entradas y devuelve el duty y los eventos.
// Los efectos (tara, NVS, microSD, publicar) los hace quien la llama.
#pragma once
#include <stdint.h>
#include "tipos.h"
#include "control_pi.h"

struct EntradaControl {
  uint32_t ahora_s;       // tiempo monótono en segundos
  bool nueva_lectura;     // true cada 10 s (adquisición); false si solo hay botón
  Boton boton;
  bool estable;           // masa estable (§7.3)
  float masa_g;
  bool cam_valida;        // SHT31 de cámara válido en esta lectura
  float t_cam_ctrl;       // mediana de las 3 últimas válidas
  float g_est;            // W/m²
  float dt_col;           // T_col − T_ent
  bool fin_detectado;     // lo pone el registro (fin_ciclo_paso)
};

struct SalidaControl {
  bool ventilador;        // interruptor (MOSFET)
  float duty;             // %
  TipoEvento ev[4];
  uint8_t n_ev;
};

constexpr uint32_t NUNCA = UINT32_MAX;

struct Maquina {
  Estado estado = Estado::ESPERA;
  Estado previo = Estado::OPERACION;   // para volver de MODO_SEGURO
  bool pausa = false;
  ControlPI pi = pi_crear();
  float duty = 0;
  bool ventilador = false;
  uint32_t t_carga = 0;                // última acción en CARGA
  uint32_t t_larga = NUNCA;            // primera pulsación larga en OPERACION
  uint8_t cam_inval = 0, cam_val = 0;
  uint32_t desde_pausa_in = NUNCA, desde_pausa_out = NUNCA, desde_fin_off = NUNCA;
};

void maquina_paso(Maquina& m, const EntradaControl& e, float setpoint, SalidaControl& s);
