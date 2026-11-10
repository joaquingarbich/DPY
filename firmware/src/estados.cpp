#include "estados.h"
#include "config.h"

static void emitir(SalidaControl& s, TipoEvento t) {
  if (s.n_ev < 4) s.ev[s.n_ev++] = t;
}

// true cuando `cond` lleva al menos `dur` s seguidos siendo cierta.
static bool sostenido(bool cond, uint32_t ahora, uint32_t& desde, uint32_t dur) {
  if (!cond) { desde = NUNCA; return false; }
  if (desde == NUNCA) desde = ahora;
  return ahora - desde >= dur;
}

static void ir_a(Maquina& m, Estado e) {
  m.estado = e;
  m.desde_pausa_in = m.desde_pausa_out = m.desde_fin_off = NUNCA;
  m.t_larga = NUNCA;
}

static void botones(Maquina& m, const EntradaControl& e, SalidaControl& s) {
  switch (m.estado) {
    case Estado::ESPERA:
      if (e.boton == Boton::CORTA && e.estable) {
        ir_a(m, Estado::CARGA);
        m.t_carga = e.ahora_s;
        emitir(s, TipoEvento::INICIO_LOTE);
      }
      break;
    case Estado::CARGA:
      if (e.boton != Boton::NINGUNO) m.t_carga = e.ahora_s;
      if (e.boton == Boton::LARGA && e.estable && e.masa_g > MASA_MIN_CARGA_G) {
        ir_a(m, Estado::OPERACION);
        m.pausa = false;
        emitir(s, TipoEvento::MASA_INICIAL);
      } else if (e.ahora_s - m.t_carga >= T_CARGA_MAX_S) {
        ir_a(m, Estado::ESPERA);
        emitir(s, TipoEvento::CARGA_CANCELADA);
      }
      break;
    case Estado::OPERACION:
      if (e.boton != Boton::LARGA) break;
      if (m.t_larga != NUNCA && e.ahora_s - m.t_larga < T_DOBLE_LARGA_S) {
        ir_a(m, Estado::ESPERA);
        m.pausa = false;
        emitir(s, TipoEvento::CIERRE_MANUAL);
      } else {
        m.t_larga = e.ahora_s;
      }
      break;
    case Estado::FIN_CICLO:
      if (e.boton == Boton::LARGA) {
        ir_a(m, Estado::ESPERA);
        emitir(s, TipoEvento::CIERRE_LOTE);
      }
      break;
    case Estado::MODO_SEGURO:
      break;
  }
}

static void sensores_y_tiempos(Maquina& m, const EntradaControl& e, SalidaControl& s) {
  // Modo seguro por el SHT31 de cámara
  if (e.cam_valida) { m.cam_inval = 0; if (m.cam_val < 255) m.cam_val++; }
  else { m.cam_val = 0; if (m.cam_inval < 255) m.cam_inval++; }

  if ((m.estado == Estado::OPERACION || m.estado == Estado::FIN_CICLO) &&
      m.cam_inval >= CAM_INVALIDAS_SEGURO) {
    m.previo = m.estado;
    ir_a(m, Estado::MODO_SEGURO);
    emitir(s, TipoEvento::MODO_SEGURO);
    return;
  }
  if (m.estado == Estado::MODO_SEGURO && m.cam_val >= CAM_VALIDAS_SALIDA) {
    ir_a(m, m.previo);
    m.pausa = false;
    emitir(s, TipoEvento::FIN_MODO_SEGURO);
  }

  if (m.estado == Estado::OPERACION && e.fin_detectado) {
    ir_a(m, Estado::FIN_CICLO);
    m.pausa = false;
    emitir(s, TipoEvento::FIN_CICLO);
    return;
  }

  if (m.estado == Estado::OPERACION) {
    if (!m.pausa && sostenido(e.g_est < G_PAUSA_IN && e.dt_col < DT_PAUSA_IN, e.ahora_s,
                              m.desde_pausa_in, T_PAUSA_IN_S)) {
      m.pausa = true;
      m.desde_pausa_out = NUNCA;
      emitir(s, TipoEvento::PAUSA_INICIO);
    } else if (m.pausa && sostenido(e.g_est > G_PAUSA_OUT || e.dt_col > DT_PAUSA_OUT, e.ahora_s,
                                    m.desde_pausa_out, T_PAUSA_OUT_S)) {
      m.pausa = false;
      m.desde_pausa_in = NUNCA;
      emitir(s, TipoEvento::PAUSA_FIN);
    }
  }
}

static void ventilador(Maquina& m, const EntradaControl& e, float sp) {
  bool estaba = m.ventilador;
  switch (m.estado) {
    case Estado::ESPERA:
    case Estado::CARGA:
      m.ventilador = false;
      break;
    case Estado::OPERACION:
      m.ventilador = !m.pausa;
      if (m.ventilador) {
        if (!estaba) pi_reiniciar(m.pi, 0);              // apagado → integral 30 %
        if (e.nueva_lectura || !estaba) m.duty = pi_paso(m.pi, e.t_cam_ctrl, sp);
      }
      break;
    case Estado::FIN_CICLO:
      if (e.g_est > G_FIN_ON) m.ventilador = true;
      if (sostenido(e.g_est < G_FIN_OFF, e.ahora_s, m.desde_fin_off, T_FIN_OFF_S)) m.ventilador = false;
      if (m.ventilador) m.duty = DUTY_MIN;
      break;
    case Estado::MODO_SEGURO:
      m.ventilador = true;
      m.duty = DUTY_MAX;
      break;
  }
  if (!m.ventilador) m.duty = 0;
}

void maquina_paso(Maquina& m, const EntradaControl& e, float setpoint, SalidaControl& s) {
  s.n_ev = 0;
  Estado antes = m.estado;
  botones(m, e, s);
  if (e.nueva_lectura) sensores_y_tiempos(m, e, s);
  // Al volver al PI desde otro modo, el integral parte del duty que había (sin salto).
  if (m.estado == Estado::OPERACION && antes != Estado::OPERACION) pi_reiniciar(m.pi, m.duty);
  ventilador(m, e, setpoint);
  s.ventilador = m.ventilador;
  s.duty = m.duty;
}
