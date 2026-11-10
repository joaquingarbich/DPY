// Simula el ESP32 SEC-01: publica un lote con datos de las últimas N horas y luego
// sigue en vivo cada 60 s. Responde a secador/SEC-01/config como el firmware.
//   node dev/simulador.js [--horas 20] [--lote L-04] [--fin]
// --fin: además emite fin_ciclo al terminar el relleno.
'use strict';
const mqtt = require('mqtt');

const arg = (k, def) => { const i = process.argv.indexOf(`--${k}`); return i > 0 ? process.argv[i + 1] : def; };
const HORAS = Number(arg('horas', 20));
const LOTE = arg('lote', 'L-04');
const FIN = process.argv.includes('--fin');
const EQ = 'SEC-01', BASE = `secador/${EQ}`;

const M0 = 6000, X0 = 0.83, XF = 0.17;
const M_SS = M0 * (1 - X0), K_H = 0.079;          // secado exponencial (1/h)
let param = { x0: X0, x0_lab: false, xf: XF, setpoint: 60, intervalo_s: 60 };
const r1 = (v) => Math.round(v * 10) / 10, r2 = (v) => Math.round(v * 100) / 100;

const c = mqtt.connect(process.env.MQTT_URL || 'mqtt://localhost:1883', {
  username: process.env.MQTT_USUARIO || 'sec-01', password: process.env.MQTT_CLAVE || 'clave-dev', clientId: EQ,
});
const pub = (t, o) => new Promise((res) => c.publish(`${BASE}/${t}`, JSON.stringify(o), { qos: 1 }, res));
const evento = (tipo, ts, datos = {}, nivel = 'info') => pub('evento', { eq: EQ, lote: LOTE, ts: ts.toISOString().slice(0, 19) + 'Z', tipo, nivel, datos });

function registro(seq, t, t0) {
  const h = (t - t0) / 3.6e6;
  const horaLocal = ((t.getUTCHours() + t.getUTCMinutes() / 60 - 5) + 24) % 24;   // Piura, UTC−5
  const sol = Math.max(0, Math.sin(((horaLocal - 6) / 12) * Math.PI));
  const G = 950 * sol, lux = Math.round(G * 46.9 * (0.95 + 0.1 * Math.random()));
  const pausa = G < 120;
  const t_ent = 24 + 8 * sol, t_col = t_ent + (pausa ? 1 : 25 * sol + 3);
  const t_cam = pausa ? t_ent + 2 : Math.min(param.setpoint + 2 * (Math.random() - 0.5), t_col - 2);
  const duty = pausa ? 0 : Math.max(30, Math.min(100, 30 + 7 * (t_cam - param.setpoint) + 40 * sol));
  const masa = M_SS + (M0 - M_SS) * Math.exp(-K_H * h) + (Math.random() - 0.5) * 3;
  return {
    eq: EQ, fw: '1.1.0', lote: LOTE, seq, ts: t.toISOString().slice(0, 19) + 'Z',
    masa_g: r1(masa), t_cam: r2(t_cam), hr_cam: r1(pausa ? 60 : 25 - 5 * sol), t_ent: r2(t_ent), hr_ent: r1(70 - 25 * sol),
    t_col: r2(t_col), luz_lux: lux, duty: r1(duty), estado: 'operacion', pausa,
    n_descartes: Math.random() < 0.05 ? 1 : 0, senal_dbm: -55 - Math.round(Math.random() * 10),
  };
}

c.on('connect', async () => {
  c.subscribe(`${BASE}/config`, { qos: 1 });
  const ahora = new Date(Math.floor(Date.now() / 60000) * 60000);
  const t0 = new Date(ahora - HORAS * 3.6e6);
  await evento('inicio_lote', new Date(t0 - 5 * 60000), { tara_cuentas: 81234 });
  await evento('masa_inicial', t0, { m0_g: M0, m_obj_g: r1(M_SS / (1 - XF)), x0: X0, xf: XF });
  let seq = 0;
  for (let t = new Date(+t0 + 60000); t <= ahora; t = new Date(+t + 60000)) await pub('telemetria', registro(++seq, t, t0));
  console.log(`relleno: ${seq} registros de ${LOTE} (${HORAS} h)`);
  if (FIN) await evento('fin_ciclo', ahora, { masa_g: 1228, m_obj_g: 1228.9 }, 'critico');
  setInterval(() => pub('telemetria', registro(++seq, new Date(Math.floor(Date.now() / 60000) * 60000), t0)), 60000);
  console.log('en vivo: un registro por minuto (Ctrl+C para salir)');
});

// Igual que el firmware (§11.3): valida, aplica y confirma.
c.on('message', async (topic, buf) => {
  let m;
  try { m = JSON.parse(buf); } catch { return; }
  if (m.lote !== LOTE) return;
  const n = { ...param, ...m };
  n.x0_lab = m.x0 !== undefined ? m.x0_lab === true : param.x0_lab;
  const ok = n.x0 >= 0.7 && n.x0 <= 0.9 && n.xf >= 0.1 && n.xf <= 0.25 && n.setpoint >= 45 && n.setpoint <= 65 && n.intervalo_s >= 60 && n.intervalo_s <= 300;
  if (!ok) return evento('config_rechazada', new Date());
  param = { x0: n.x0, x0_lab: n.x0_lab, xf: n.xf, setpoint: n.setpoint, intervalo_s: n.intervalo_s };
  console.log('config aplicada', param);
  await evento('config_aplicada', new Date(), { ...param, m_obj_g: r1(M0 * (1 - param.x0) / (1 - param.xf)) });
});
