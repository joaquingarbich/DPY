'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { MongoMemoryServer } = require('mongodb-memory-server');
const s = require('../nodered/lib/secador');

const registro = (o = {}) => ({
  eq: 'SEC-01', fw: '1.1.0', lote: 'L-04', seq: 1, ts: '2026-10-01T21:07:00Z',
  masa_g: 2873.4, t_cam: 54.62, hr_cam: 23.1, t_ent: 31.18, hr_ent: 47.0, t_col: 63.94,
  luz_lux: 41260, duty: 30.0, estado: 'operacion', pausa: false, n_descartes: 0, senal_dbm: -61, ...o,
});
const evento = (tipo, datos = {}, o = {}) => ({ eq: 'SEC-01', lote: 'L-04', ts: '2026-10-01T10:00:00Z', tipo, nivel: 'info', datos, ...o });
const auth = (u, c) => ({ authorization: 'Basic ' + Buffer.from(`${u}:${c}`).toString('base64') });
const TEC = auth('tecnico', 't-clave'), GRA = auth('grafana', 'g-clave');

// ---------------------------------------------------------------- funciones puras
test('validarRegistro acepta el ejemplo del documento y null', () => {
  assert.equal(s.validarRegistro(registro()), null);
  assert.equal(s.validarRegistro(registro({ ts: null, t_col: null, senal_dbm: null })), null);
});

test('validarRegistro rechaza tipos, faltantes y rangos', () => {
  assert.match(s.validarRegistro(registro({ t_cam: 95 })), /t_cam fuera de rango/);
  assert.match(s.validarRegistro(registro({ masa_g: '12' })), /masa_g no es número/);
  assert.match(s.validarRegistro(registro({ seq: 0 })), /seq/);
  assert.match(s.validarRegistro(registro({ estado: 'x' })), /estado/);
  assert.match(s.validarRegistro(registro({ ts: 'ayer' })), /ts/);
  const sinLuz = registro(); delete sinLuz.luz_lux;
  assert.match(s.validarRegistro(sinLuz), /falta luz_lux/);
  assert.match(s.validarRegistro(null), /objeto/);
});

test('interpolar: lineal y saturada en los extremos', () => {
  const c = [{ duty: 30, kg_s: 0.006 }, { duty: 100, kg_s: 0.013 }];
  assert.equal(s.interpolar(c, 30), 0.006);
  assert.ok(Math.abs(s.interpolar(c, 65) - 0.0095) < 1e-12);
  assert.equal(s.interpolar(c, 10), 0.006);
  assert.equal(s.interpolar(c, 120), 0.013);
  assert.equal(s.interpolar(c, null), null);
});

test('derivadosFijos: G_est y eta solo con G > 200 y duty > 0', () => {
  const cal = { K: 46.9, curva_caudal: [{ duty: 0, kg_s: 0 }, { duty: 100, kg_s: 0.02 }] };
  const d = s.derivadosFijos(registro({ luz_lux: 46900, duty: 50, t_col: 60, t_ent: 30 }), cal);
  assert.equal(d.g_est, 1000);
  assert.equal(d.m_dot, 0.01);
  assert.ok(Math.abs(d.eta - (0.01 * 1006 * 30) / (0.65 * 1000)) < 1e-12);
  assert.equal(s.derivadosFijos(registro({ luz_lux: 4690, duty: 50 }), cal).eta, null);  // 100 W/m²
  assert.equal(s.derivadosFijos(registro({ duty: 0 }), cal).eta, null);
  assert.equal(s.derivadosFijos(registro({ luz_lux: null }), cal).g_est, null);
});

test('derivadosLote: fórmulas del §15.3 y null sin humedad de laboratorio', () => {
  const lote = { m0: 6000, x0: 0.8, xf: 0.17, x0_lab: true };
  const m_ss = 1200;
  const d = s.derivadosLote(3000, lote);
  assert.ok(Math.abs(d.x_bh - (1 - m_ss / 3000)) < 1e-12);
  assert.ok(Math.abs(d.x_bs - 1.5) < 1e-12);
  assert.ok(Math.abs(d.mr - 1.5 / 4) < 1e-12);
  const m_obj = s.masaObjetivo(6000, 0.8, 0.17);
  assert.ok(Math.abs(d.avance - 3000 / (6000 - m_obj)) < 1e-12);
  assert.deepEqual(s.derivadosLote(3000, { ...lote, x0_lab: false }), { x_bh: null, x_bs: null, mr: null, avance: null });
  assert.equal(s.derivadosLote(null, lote).x_bh, null);
});

test('validarConfig usa los rangos del firmware', () => {
  assert.equal(s.validarConfig({ x0: 0.83, xf: 0.17, setpoint: 60, intervalo_s: 60 }), null);
  assert.match(s.validarConfig({ setpoint: 70 }), /setpoint/);
  assert.match(s.validarConfig({ intervalo_s: 90.5 }), /entero/);
  assert.equal(s.validarConfig({}), null);
});

test('aCsv escapa comas y comillas', () => {
  const csv = s.aCsv([{ a: 1, b: 'x,"y"', c: null, d: new Date('2026-10-01T00:00:00Z') }], ['a', 'b', 'c', 'd']);
  assert.equal(csv, 'a,b,c,d\n1,"x,""y""",,2026-10-01T00:00:00.000Z\n');
});

// ---------------------------------------------------------------- integración con MongoDB
test('flujo completo contra MongoDB', async (t) => {
  const mongo = await MongoMemoryServer.create({ binary: { version: '8.0.4' } });
  let reloj = new Date('2026-10-01T12:00:00Z');
  const srv = s.crear({ uri: mongo.getUri(), usuarios: { tecnico: 't-clave', grafana: 'g-clave' }, ahora: () => reloj });
  t.after(async () => { await srv.cerrar(); await mongo.stop(); });
  const db = await srv.base();

  await t.test('crea colecciones, serie de tiempo y calibración provisional', async () => {
    const cols = await db.listCollections().toArray();
    assert.equal(cols.find((c) => c.name === 'mediciones').type, 'timeseries');
    const cal = await db.collection('calibracion').findOne({ _id: 'actual' });
    assert.equal(cal.provisional, true);
  });

  await t.test('sin lote creado, la telemetría va a rechazados', async () => {
    const r = await srv.ingresarTelemetria(registro());
    assert.deepEqual(r, { ok: false, motivo: 'lote inexistente' });
    assert.equal(await db.collection('rechazados').countDocuments(), 1);
  });

  await t.test('inicio_lote y masa_inicial crean el lote', async () => {
    await srv.ingresarEvento(evento('inicio_lote', { tara_cuentas: 81234 }));
    let l = await db.collection('lotes').findOne({ _id: 'L-04' });
    assert.equal(l.estado, 'carga');
    await srv.ingresarEvento(evento('masa_inicial', { m0_g: 6000, m_obj_g: 1228.9, x0: 0.83, xf: 0.17 }, { ts: '2026-10-01T10:05:00Z' }));
    l = await db.collection('lotes').findOne({ _id: 'L-04' });
    assert.equal(l.estado, 'operacion');
    assert.equal(l.m0, 6000);
    assert.equal(l.x0_lab, false);
  });

  await t.test('ingreso, derivados guardados y deduplicación', async () => {
    const r = await srv.ingresarTelemetria(registro({ seq: 1, ts: '2026-10-01T10:06:00Z' }));
    assert.equal(r.ok, true);
    assert.ok(Math.abs(r.doc.g_est - 41260 / 46.9) < 1e-9);
    assert.ok(r.doc.eta > 0);
    const dup = await srv.ingresarTelemetria(registro({ seq: 1, ts: '2026-10-01T10:06:00Z' }));
    assert.equal(dup.duplicado, true);
    // reinicio de Node-RED: otra instancia sin memoria también lo detecta (consulta MongoDB)
    const srv2 = s.crear({ uri: mongo.getUri() });
    assert.equal((await srv2.ingresarTelemetria(registro({ seq: 1, ts: '2026-10-01T10:06:00Z' }))).duplicado, true);
    await srv2.cerrar();
    assert.equal(await db.collection('mediciones').countDocuments(), 1);
  });

  await t.test('otro equipo no puede escribir en el lote', async () => {
    const r = await srv.ingresarTelemetria(registro({ eq: 'SEC-02', seq: 99 }), 'secador/SEC-02/telemetria');
    assert.equal(r.motivo, 'lote de otro equipo');
    const e = await srv.ingresarEvento(evento('cierre_lote', {}, { eq: 'SEC-02' }), 'secador/SEC-02/evento');
    assert.equal(e.ok, false);
    assert.equal((await db.collection('lotes').findOne({ _id: 'L-04' })).estado, 'operacion');
  });

  await t.test('ts null: hora del servidor marcada', async () => {
    const r = await srv.ingresarTelemetria(registro({ seq: 2, ts: null }));
    assert.equal(r.doc.ts_servidor, true);
    assert.equal(r.doc.ts.getTime(), reloj.getTime());
  });

  await t.test('autenticación y permisos', async () => {
    assert.equal((await srv.http('GET /api/lotes', { headers: {} })).status, 401);
    assert.equal((await srv.http('GET /api/lotes', { headers: auth('grafana', 'mal') })).status, 401);
    assert.equal((await srv.http('GET /api/lotes', { headers: GRA })).status, 200);
    const p = await srv.http('POST /api/lotes/:id/config', { headers: GRA, params: { id: 'L-04' }, body: { setpoint: 55 } });
    assert.equal(p.status, 403);
    // /api/yo y la página /app: sin ventana del navegador si lo pide la página
    assert.deepEqual((await srv.http('GET /api/yo', { headers: TEC })).body, { rol: 'tecnico' });
    assert.ok((await srv.http('GET /api/yo', { headers: {} })).headers['www-authenticate']);
    assert.deepEqual((await srv.http('GET /api/yo', { headers: { 'x-requested-with': 'secador-app' } })).headers, {});
  });

  await t.test('sin gravimetría, los derivados de x0 son null', async () => {
    const r = await srv.http('GET /api/series', { headers: GRA, query: { lote: 'L-04', campos: 'masa_g,x_bh,avance' } });
    assert.equal(r.status, 200);
    assert.equal(r.body.length, 2);
    assert.equal(r.body[0].masa_g, 2873.4);
    assert.equal(r.body[0].x_bh, null);
    assert.deepEqual(Object.keys(r.body[0]), ['ts', 'masa_g', 'x_bh', 'avance']);
  });

  await t.test('gravimetría inicial: x0_lab, m_obj y config retenida con x0_lab', async () => {
    const bad = await srv.http('POST /api/lotes/:id/gravimetria/inicial', { headers: TEC, params: { id: 'L-04' }, body: { x0: 0.95 } });
    assert.equal(bad.status, 400);
    const r = await srv.http('POST /api/lotes/:id/gravimetria/inicial', { headers: TEC, params: { id: 'L-04' }, body: { x0: 0.8281 } });
    assert.equal(r.status, 200);
    assert.equal(r.mqtt.topic, 'secador/SEC-01/config');
    assert.equal(r.mqtt.retain, true);
    const msg = JSON.parse(r.mqtt.payload);
    assert.deepEqual(msg, { lote: 'L-04', x0: 0.8281, x0_lab: true, xf: 0.17, setpoint: 60, intervalo_s: 60 });
    const l = await db.collection('lotes').findOne({ _id: 'L-04' });
    assert.ok(Math.abs(l.m_obj - s.masaObjetivo(6000, 0.8281, 0.17)) < 1e-9);
    // ahora los derivados aparecen, recalculados para los registros anteriores
    const serie = await srv.http('GET /api/series', { headers: GRA, query: { lote: 'L-04', campos: 'x_bh,avance' } });
    assert.ok(serie.body[0].x_bh > 0.5 && serie.body[0].x_bh < 0.83);
  });

  await t.test('config: valida, guarda y publica; x0 a mano vuelve a provisional', async () => {
    const bad = await srv.http('POST /api/lotes/:id/config', { headers: TEC, params: { id: 'L-04' }, body: { setpoint: 70 } });
    assert.equal(bad.status, 400);
    const r = await srv.http('POST /api/lotes/:id/config', { headers: TEC, params: { id: 'L-04' }, body: { setpoint: 58, intervalo_s: 120 } });
    assert.equal(r.status, 200);
    const msg = JSON.parse(r.mqtt.payload);
    assert.equal(msg.setpoint, 58);
    assert.equal(msg.x0_lab, true);                  // se conserva la gravimetría
    assert.equal((await srv.http('POST /api/lotes/:id/config', { headers: TEC, params: { id: 'L-99' }, body: {} })).status, 404);
  });

  await t.test('config_aplicada del equipo actualiza el lote', async () => {
    await srv.ingresarEvento(evento('config_aplicada', { x0: 0.8281, x0_lab: true, xf: 0.17, setpoint: 58, intervalo_s: 120, m_obj_g: 1150.2 }));
    const l = await db.collection('lotes').findOne({ _id: 'L-04' });
    assert.equal(l.intervalo_s, 120);
    assert.equal(l.m_obj, 1150.2);
  });

  await t.test('/api/ultimo: en línea, sin datos y alertas', async () => {
    reloj = new Date('2026-10-01T12:01:00Z');
    let u = (await srv.http('GET /api/ultimo', { headers: GRA, query: {} })).body;
    assert.equal(u.length, 1);
    assert.equal(u[0].eq, 'SEC-01');
    assert.equal(u[0].lote_activo, 1);
    assert.equal(u[0].alerta_fin_ciclo, 0);
    assert.equal(u[0].estado_panel, 'operacion');
    reloj = new Date('2026-10-01T14:00:00Z');        // 2 h después del último registro (12:00)
    u = (await srv.http('GET /api/ultimo', { headers: GRA, query: { eq: 'SEC-01' } })).body;
    assert.ok(u[0].min_sin_datos > 5);
    assert.equal(u[0].en_linea, 0);
    assert.equal(u[0].estado_panel, 'sin_conexion');
    await srv.ingresarEvento(evento('fin_ciclo', { masa_g: 1149, m_obj_g: 1150.2 }, { nivel: 'critico' }));
    u = (await srv.http('GET /api/ultimo', { headers: GRA, query: { eq: 'SEC-01' } })).body;
    assert.equal(u[0].alerta_fin_ciclo, 1);
    await srv.ingresarEvento(evento('modo_seguro', { previo: 'fin_ciclo' }));
    await srv.ingresarEvento(evento('fin_modo_seguro'));
    const l = await db.collection('lotes').findOne({ _id: 'L-04' });
    assert.equal(l.estado, 'fin_ciclo');             // vuelve al estado previo
  });

  await t.test('cierre: sin alertas ni sin_datos', async () => {
    await srv.ingresarEvento(evento('cierre_lote', { motivo: 'fin_ciclo', seq: 2 }, { ts: '2026-10-01T15:00:00Z' }));
    const u = (await srv.http('GET /api/ultimo', { headers: GRA, query: { eq: 'SEC-01' } })).body;
    assert.equal(u[0].lote_estado, 'cerrado');
    assert.equal(u[0].lote_activo, 0);
    assert.equal(u[0].min_sin_datos, 0);
    assert.equal(u[0].alerta_fin_ciclo, 0);
  });

  await t.test('eventos, rechazados y CSV', async () => {
    const e = (await srv.http('GET /api/eventos', { headers: GRA, query: { lote: 'L-04' } })).body;
    assert.equal(e[0].tipo, 'cierre_lote');
    assert.equal(typeof e[0].datos, 'string');
    assert.equal((await srv.http('GET /api/eventos', { headers: GRA, query: { eq: 'SEC-01', limite: '2' } })).body.length, 2);
    const r = (await srv.http('GET /api/rechazados', { headers: GRA, query: {} })).body;
    assert.ok(r.some((x) => x.motivo === 'lote inexistente'));
    const csv = await srv.http('GET /api/export.csv', { headers: GRA, query: { lote: 'L-04' } });
    assert.match(csv.headers['content-type'], /text\/csv/);
    const lineas = csv.body.trim().split('\n');
    assert.equal(lineas.length, 3);
    assert.ok(lineas[0].startsWith('ts,seq,estado,pausa,masa_g'));
    assert.equal((await srv.http('GET /api/export.csv', { headers: GRA, query: {} })).status, 400);
  });

  await t.test('evento inválido va a rechazados', async () => {
    const r = await srv.ingresarEvento({ eq: 'SEC-01' });
    assert.equal(r.ok, false);
  });
});

test('eventos y telemetría simultáneos se procesan en orden', async (t) => {
  const mongo = await MongoMemoryServer.create({ binary: { version: '8.0.4' } });
  const srv = s.crear({ uri: mongo.getUri() });
  t.after(async () => { await srv.cerrar(); await mongo.stop(); });
  // Como llegan desde MQTT: sin esperar a que termine el anterior.
  const p = [srv.ingresarEvento(evento('masa_inicial', { m0_g: 6000, m_obj_g: 1228.9, x0: 0.83, xf: 0.17 }))];
  for (let i = 1; i <= 50; i++) p.push(srv.ingresarTelemetria(registro({ seq: i, ts: new Date(Date.UTC(2026, 9, 1, 11, i)).toISOString() })));
  const r = await Promise.all(p);
  assert.ok(r.every((x) => x.ok), r.find((x) => !x.ok)?.motivo);
});

test('seguridad: eq del tópico y formato de eq', () => {
  assert.equal(s.eqDelTopico('secador/SEC-01/telemetria', 'SEC-01'), null);
  assert.match(s.eqDelTopico('secador/SEC-02/telemetria', 'SEC-01'), /tópico/);
  assert.match(s.validarRegistro(registro({ eq: 'SEC-01/#' })), /eq inválido/);
  assert.match(s.validarEvento({ eq: '../x', tipo: 'fin_ciclo', lote: null, ts: null }), /eq inválido/);
});
