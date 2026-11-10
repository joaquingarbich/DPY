// Lógica del servidor del secador (documentación §15). Los nodos de Node-RED solo
// la llaman: así se prueba con `node --test` sin levantar Node-RED.
'use strict';
const { MongoClient } = require('mongodb');
const crypto = require('crypto');

// ---------------------------------------------------------------- constantes
const AREA_COLECTOR_M2 = 0.65;
const CP_AIRE = 1006;                       // J/(kg·K)
const ESTADOS = ['espera', 'carga', 'operacion', 'fin_ciclo', 'modo_seguro'];
const LOTE_ACTIVO = ['operacion', 'fin_ciclo', 'modo_seguro'];
const DEDUP_MS = 24 * 3600 * 1000;

// Rangos físicos: los mismos del firmware (§7.1). null siempre es válido.
const RANGOS = {
  masa_g: [-200, 10500], t_cam: [-10, 90], hr_cam: [0, 100], t_ent: [-10, 90], hr_ent: [0, 100],
  t_col: [-10, 125], luz_lux: [0, 65535], duty: [0, 100], senal_dbm: [-127, 0],
};
const CONFIG = { x0: [0.70, 0.90], xf: [0.10, 0.25], setpoint: [45, 65], intervalo_s: [60, 300] };

const CALIBRACION_INICIAL = {
  _id: 'actual', S: 211.34, C_T: 0.465, K: 46.9,
  // Curva de ejemplo: reemplazar con la medida (§5.3 del informe) y quitar "provisional".
  curva_caudal: [{ duty: 30, kg_s: 0.006 }, { duty: 50, kg_s: 0.009 }, { duty: 75, kg_s: 0.012 }, { duty: 100, kg_s: 0.014 }],
  provisional: true,
};

// ---------------------------------------------------------------- funciones puras
const esNum = (v) => typeof v === 'number' && Number.isFinite(v);

const EQ_VALIDO = /^[A-Z0-9-]{1,20}$/;

// El equipo solo puede publicar en su propio tópico: secador/<eq>/...
function eqDelTopico(topico, eq) {
  if (topico === undefined) return null;   // llamadas internas y pruebas
  const t = String(topico).split('/');
  return t[1] === eq ? null : 'eq no coincide con el tópico';
}

function validarRegistro(r) {
  if (!r || typeof r !== 'object') return 'no es un objeto';
  if (!EQ_VALIDO.test(r.eq || '')) return 'eq inválido';
  for (const k of ['eq', 'fw', 'lote']) if (typeof r[k] !== 'string' || !r[k]) return `falta ${k}`;
  if (!Number.isInteger(r.seq) || r.seq < 1) return 'seq inválido';
  if (r.ts !== null && (typeof r.ts !== 'string' || Number.isNaN(Date.parse(r.ts)))) return 'ts inválido';
  if (!ESTADOS.includes(r.estado)) return 'estado inválido';
  if (typeof r.pausa !== 'boolean') return 'pausa inválida';
  if (!Number.isInteger(r.n_descartes) || r.n_descartes < 0) return 'n_descartes inválido';
  for (const [k, [lo, hi]] of Object.entries(RANGOS)) {
    if (!(k in r)) return `falta ${k}`;
    if (r[k] === null) continue;
    if (!esNum(r[k])) return `${k} no es número`;
    if (r[k] < lo || r[k] > hi) return `${k} fuera de rango`;
  }
  return null;
}

function validarEvento(e) {
  if (!e || typeof e !== 'object') return 'no es un objeto';
  if (typeof e.eq !== 'string' || !EQ_VALIDO.test(e.eq)) return 'eq inválido';
  if (typeof e.tipo !== 'string' || !e.tipo) return 'falta tipo';
  if (e.lote !== null && typeof e.lote !== 'string') return 'lote inválido';
  if (e.ts !== null && (typeof e.ts !== 'string' || Number.isNaN(Date.parse(e.ts)))) return 'ts inválido';
  return null;
}

// Interpolación lineal; fuera de la curva se satura en los extremos.
function interpolar(curva, x) {
  if (!esNum(x) || !curva || !curva.length) return null;
  const c = [...curva].sort((a, b) => a.duty - b.duty);
  if (x <= c[0].duty) return c[0].kg_s;
  for (let i = 1; i < c.length; i++) {
    if (x <= c[i].duty) {
      const f = (x - c[i - 1].duty) / (c[i].duty - c[i - 1].duty);
      return c[i - 1].kg_s + f * (c[i].kg_s - c[i - 1].kg_s);
    }
  }
  return c[c.length - 1].kg_s;
}

const masaObjetivo = (m0, x0, xf) => (m0 * (1 - x0)) / (1 - xf);

// Derivados que no dependen de la humedad de laboratorio: se guardan al insertar.
function derivadosFijos(r, cal) {
  const g_est = esNum(r.luz_lux) && cal.K > 0 ? r.luz_lux / cal.K : null;
  const m_dot = r.duty > 0 ? interpolar(cal.curva_caudal, r.duty) : 0;
  let eta = null;
  if (g_est > 200 && r.duty > 0 && esNum(r.t_col) && esNum(r.t_ent) && esNum(m_dot))
    eta = (m_dot * CP_AIRE * (r.t_col - r.t_ent)) / (AREA_COLECTOR_M2 * g_est);
  return { g_est, m_dot, eta };
}

// Derivados que dependen de x0: se calculan al leer, con los datos vigentes del lote,
// y valen null mientras no llegue la humedad de laboratorio.
function derivadosLote(masa, lote) {
  const nulos = { x_bh: null, x_bs: null, mr: null, avance: null };
  if (!lote || !lote.x0_lab || !esNum(masa) || !esNum(lote.m0) || !esNum(lote.x0)) return nulos;
  const m_ss = lote.m0 * (1 - lote.x0);
  const x_bs0 = lote.x0 / (1 - lote.x0);
  const m_obj = esNum(lote.m_obj) ? lote.m_obj : masaObjetivo(lote.m0, lote.x0, lote.xf);
  return {
    x_bh: 1 - m_ss / masa,
    x_bs: (masa - m_ss) / m_ss,
    mr: (masa - m_ss) / m_ss / x_bs0,
    avance: lote.m0 !== m_obj ? (lote.m0 - masa) / (lote.m0 - m_obj) : null,
  };
}

function validarConfig(c) {
  for (const [k, [lo, hi]] of Object.entries(CONFIG)) {
    if (c[k] === undefined) continue;
    if (!esNum(c[k]) || c[k] < lo || c[k] > hi) return `${k} fuera de rango (${lo} a ${hi})`;
  }
  if (c.intervalo_s !== undefined && !Number.isInteger(c.intervalo_s)) return 'intervalo_s debe ser entero';
  return null;
}

function aCsv(filas, columnas) {
  const celda = (v) => {
    if (v === null || v === undefined) return '';
    if (v instanceof Date) return v.toISOString();
    const s = String(v);
    return /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
  };
  return [columnas.join(','), ...filas.map((f) => columnas.map((c) => celda(f[c])).join(','))].join('\n') + '\n';
}

function iguales(a, b) {
  const x = Buffer.from(String(a)), y = Buffer.from(String(b));
  return x.length === y.length && crypto.timingSafeEqual(x, y);
}

// ---------------------------------------------------------------- servicio
const CAMPOS_REGISTRO = ['masa_g', 't_cam', 'hr_cam', 't_ent', 'hr_ent', 't_col', 'luz_lux', 'duty', 'n_descartes', 'senal_dbm'];
const CAMPOS_DERIVADOS = ['g_est', 'm_dot', 'eta', 'x_bh', 'x_bs', 'mr', 'avance'];
const CAMPOS_SERIE = [...CAMPOS_REGISTRO, ...CAMPOS_DERIVADOS, 'seq', 'estado', 'pausa', 'm_obj', 'setpoint'];

function crear({ uri, db: nombreDb = 'secador', usuarios = {}, ahora = () => new Date() } = {}) {
  let cliente, db, listo;
  let cal = null, calHasta = 0;
  const vistos = new Map();           // clave de deduplicación → hora
  const inicioProceso = Date.now();
  const vigilancia = new Map();       // eq → timer de sin_datos
  const stats = { duplicados: 0 };
  // Telemetría y eventos llegan por tópicos distintos y Node-RED los atiende en paralelo.
  // Se procesan de a uno, en orden de llegada, para que masa_inicial cree el lote antes
  // de que se valide la primera telemetría.
  let cola = Promise.resolve();
  const enOrden = (f) => (...a) => (cola = cola.then(() => f(...a), () => f(...a)));

  async function base() {
    if (!listo) listo = (async () => {
      cliente = new MongoClient(uri);
      await cliente.connect();
      db = cliente.db(nombreDb);
      await prepararBase();
    })().catch((e) => { listo = null; throw e; });
    await listo;
    return db;
  }

  // Crea colecciones e índices si faltan (§15.2). Es idempotente.
  async function prepararBase() {
    const existentes = new Set((await db.listCollections({}, { nameOnly: true }).toArray()).map((c) => c.name));
    if (!existentes.has('mediciones'))
      await db.createCollection('mediciones', { timeseries: { timeField: 'ts', metaField: 'meta', granularity: 'minutes' } });
    for (const c of ['eventos', 'lotes', 'calibracion', 'rechazados']) if (!existentes.has(c)) await db.createCollection(c);
    await db.collection('eventos').createIndex({ lote: 1, ts: 1 });
    await db.collection('rechazados').createIndex({ recibido: 1 });
    await db.collection('calibracion').updateOne({ _id: 'actual' }, { $setOnInsert: CALIBRACION_INICIAL }, { upsert: true });
  }

  async function calibracion() {
    if (!cal || Date.now() > calHasta) {
      cal = await (await base()).collection('calibracion').findOne({ _id: 'actual' });
      calHasta = Date.now() + 60000;
    }
    return cal;
  }

  async function rechazar(payload, motivo) {
    await (await base()).collection('rechazados').insertOne({ recibido: ahora(), motivo, payload });
    return { ok: false, motivo };
  }

  // ---------------- ingreso de telemetría (validar → deduplicar → calcular → insertar)
  async function ingresarTelemetria(r, topico) {
    const d = await base();
    const motivo = validarRegistro(r) || eqDelTopico(topico, r.eq);
    if (motivo) return rechazar(r, motivo);
    const lote = await d.collection('lotes').findOne({ _id: r.lote });
    if (!lote) return rechazar(r, 'lote inexistente');
    if (lote.eq && lote.eq !== r.eq) return rechazar(r, 'lote de otro equipo');

    const clave = `${r.eq}|${r.lote}|${r.seq}|${r.ts}`;
    const t = Date.now();
    for (const [k, v] of vistos) if (t - v > DEDUP_MS) vistos.delete(k); else break;
    let dup = vistos.has(clave);
    if (!dup && t - inicioProceso < DEDUP_MS) {   // el conjunto se reinició: preguntar a MongoDB
      dup = !!(await d.collection('mediciones').findOne(
        { 'meta.eq': r.eq, 'meta.lote': r.lote, seq: r.seq }, { projection: { _id: 1 } }));
    }
    vistos.set(clave, t);
    if (dup) { stats.duplicados++; return { ok: true, duplicado: true }; }

    const recibido = ahora();
    const doc = {
      ts: r.ts ? new Date(r.ts) : recibido, meta: { eq: r.eq, lote: r.lote }, seq: r.seq, fw: r.fw,
      estado: r.estado, pausa: r.pausa, recibido,
      ...Object.fromEntries(CAMPOS_REGISTRO.map((k) => [k, r[k]])),
      ...derivadosFijos(r, await calibracion()),
    };
    if (!r.ts) doc.ts_servidor = true;
    await d.collection('mediciones').insertOne(doc);
    vigilar(r.eq, r.lote, lote.intervalo_s || 60);
    return { ok: true, doc };
  }

  // ---------------- sin_datos: 5 intervalos sin telemetría con lote activo (§15.3)
  function vigilar(eq, lote, intervaloS) {
    clearTimeout(vigilancia.get(eq));
    const timer = setTimeout(async () => {
      try {
        const d = await base();
        const l = await d.collection('lotes').findOne({ _id: lote });
        if (l && LOTE_ACTIVO.includes(l.estado))
          await d.collection('eventos').insertOne({ eq, lote, ts: ahora(), tipo: 'sin_datos', nivel: 'critico', datos: { intervalo_s: intervaloS }, recibido: ahora() });
      } catch (e) { console.error('sin_datos:', e.message); }
    }, 5 * intervaloS * 1000);
    timer.unref?.();
    vigilancia.set(eq, timer);
  }

  // ---------------- eventos y ciclo de vida del lote
  async function ingresarEvento(e, topico) {
    const d = await base();
    const motivo = validarEvento(e) || eqDelTopico(topico, e.eq);
    if (motivo) return rechazar(e, `evento: ${motivo}`);
    const ts = e.ts ? new Date(e.ts) : ahora();
    const datos = e.datos || {};
    if (e.lote) {
      // Un equipo no puede tocar el lote de otro.
      // ponytail: los id L-xx son por equipo; con varios secadores conviene prefijar el eq.
      const dueno = await d.collection('lotes').findOne({ _id: e.lote }, { projection: { eq: 1 } });
      if (dueno && dueno.eq && dueno.eq !== e.eq) return rechazar(e, 'evento: lote de otro equipo');
    }
    await d.collection('eventos').insertOne({ eq: e.eq, lote: e.lote, ts, tipo: e.tipo, nivel: e.nivel || 'info', datos, recibido: ahora() });
    if (!e.lote) return { ok: true };

    const lotes = d.collection('lotes');
    const set = (campos) => lotes.updateOne({ _id: e.lote }, { $set: campos });
    switch (e.tipo) {
      case 'inicio_lote':
        await lotes.updateOne({ _id: e.lote }, {
          $set: { inicio: ts, estado: 'carga', tara_cuentas: datos.tara_cuentas ?? null },
          $setOnInsert: { eq: e.eq, x0_lab: false, setpoint: 60, intervalo_s: 60 },
        }, { upsert: true });
        break;
      case 'masa_inicial':
        await lotes.updateOne({ _id: e.lote }, {
          $set: { estado: 'operacion', inicio_operacion: ts, m0: datos.m0_g, x0: datos.x0, xf: datos.xf, m_obj: datos.m_obj_g },
          $setOnInsert: { eq: e.eq, inicio: ts, x0_lab: false, setpoint: 60, intervalo_s: 60 },
        }, { upsert: true });
        break;
      case 'carga_cancelada': await set({ estado: 'cancelado', cierre: ts }); break;
      case 'fin_ciclo': await set({ estado: 'fin_ciclo', fin_ciclo: ts }); break;
      case 'modo_seguro': await set({ estado: 'modo_seguro', estado_previo: datos.previo || 'operacion' }); break;
      case 'fin_modo_seguro': {
        const l = await lotes.findOne({ _id: e.lote });
        await set({ estado: (l && l.estado_previo) || 'operacion' });
        break;
      }
      case 'cierre_lote': await set({ estado: 'cerrado', cierre: ts, motivo_cierre: datos.motivo || null }); break;
      case 'reanudacion': if (LOTE_ACTIVO.includes(datos.estado)) await set({ estado: datos.estado }); break;
      case 'config_aplicada': {
        if (validarConfig(datos)) break;   // valores fuera de rango: solo queda el evento
        const c = {};
        for (const k of ['x0', 'x0_lab', 'xf', 'setpoint', 'intervalo_s']) if (datos[k] !== undefined) c[k] = datos[k];
        if (esNum(datos.m_obj_g) && datos.m_obj_g > 0) c.m_obj = datos.m_obj_g;
        await set(c);
        break;
      }
    }
    return { ok: true };
  }

  // ---------------- HTTP: autenticación básica con dos usuarios
  function rolDe(req) {
    const h = req && req.headers && req.headers.authorization;
    if (!h || !h.startsWith('Basic ')) return null;
    const [u, ...resto] = Buffer.from(h.slice(6), 'base64').toString().split(':');
    const clave = resto.join(':');
    if (usuarios.tecnico && u === 'tecnico' && iguales(clave, usuarios.tecnico)) return 'tecnico';
    if (usuarios.grafana && u === 'grafana' && iguales(clave, usuarios.grafana)) return 'lectura';
    if (usuarios.operario && u === 'operario' && iguales(clave, usuarios.operario)) return 'lectura';
    return null;
  }

  const json = (status, body) => ({ status, headers: { 'content-type': 'application/json' }, body });
  const fecha = (v, def) => {
    if (v === undefined || v === '') return def;
    const n = Number(v);
    const f = new Date(Number.isFinite(n) ? n : v);
    return Number.isNaN(f.getTime()) ? def : f;
  };

  function fila(m, lote) {
    const f = { ts: m.ts, seq: m.seq, estado: m.estado, pausa: m.pausa };
    for (const k of [...CAMPOS_REGISTRO, 'g_est', 'm_dot', 'eta']) f[k] = m[k] ?? null;
    Object.assign(f, derivadosLote(m.masa_g, lote));
    f.m_obj = lote && lote.x0_lab ? lote.m_obj ?? null : null;
    f.setpoint = lote ? lote.setpoint ?? null : null;
    return f;
  }

  async function publicarConfig(lote) {
    return {
      topic: `secador/${lote.eq}/config`, retain: true, qos: 1,
      payload: JSON.stringify({ lote: lote._id, x0: lote.x0, x0_lab: !!lote.x0_lab, xf: lote.xf, setpoint: lote.setpoint, intervalo_s: lote.intervalo_s }),
    };
  }

  const rutas = {
    async 'GET /api/lotes'(q) {
      const l = await (await base()).collection('lotes').find(q.eq ? { eq: q.eq } : {}).sort({ inicio: -1 }).toArray();
      return json(200, l.map((x) => ({ id: x._id, ...x, _id: undefined })));
    },

    async 'GET /api/series'(q) {
      if (!q.lote) return json(400, { error: 'falta lote' });
      const d = await base();
      const lote = await d.collection('lotes').findOne({ _id: q.lote });
      const campos = q.campos ? String(q.campos).split(',').filter((c) => CAMPOS_SERIE.includes(c)) : CAMPOS_SERIE;
      const filtro = { 'meta.lote': q.lote, ts: { $gte: fecha(q.desde, new Date(0)), $lte: fecha(q.hasta, new Date(8.64e15)) } };
      const m = await d.collection('mediciones').find(filtro).sort({ ts: 1 }).toArray();
      return json(200, m.map((x) => {
        const f = fila(x, lote), o = { ts: f.ts };
        for (const c of campos) o[c] = f[c];
        return o;
      }));
    },

    // Una fila por equipo (o solo `eq`), con los campos que usan las alertas de Grafana.
    async 'GET /api/ultimo'(q) {
      const d = await base();
      const eqs = q.eq ? [q.eq] : await d.collection('lotes').distinct('eq');
      const filas = [];
      for (const eq of eqs) {
        const lote = await d.collection('lotes').find({ eq }).sort({ inicio: -1 }).limit(1).next();
        const m = await d.collection('mediciones').find({ 'meta.eq': eq }).sort({ ts: -1 }).limit(1).next();
        const activo = !!lote && LOTE_ACTIVO.includes(lote.estado);
        const desde = m ? m.ts : lote && lote.inicio_operacion;
        const min = desde ? (ahora() - desde) / 60000 : null;
        const intervaloMin = ((lote && lote.intervalo_s) || 60) / 60;
        const enLinea = activo && min !== null && min <= 2 * intervaloMin;
        // Una sola palabra para el panel del operario.
        let estadoPanel = lote ? lote.estado : 'sin_lote';
        if (activo && !enLinea) estadoPanel = 'sin_conexion';
        else if (lote && lote.estado === 'operacion' && m && m.meta.lote === lote._id && m.pausa) estadoPanel = 'pausa';
        filas.push({
          eq, ...(m && lote && m.meta.lote === lote._id ? fila(m, lote) : { ts: m ? m.ts : null }),
          lote: lote ? lote._id : null, lote_estado: lote ? lote.estado : null, lote_activo: activo ? 1 : 0,
          m0: lote ? lote.m0 ?? null : null, x0: lote ? lote.x0 ?? null : null, x0_lab: lote ? !!lote.x0_lab : false,
          inicio_operacion: lote ? lote.inicio_operacion ?? null : null,
          horas_lote: lote && lote.inicio_operacion ? (ahora() - lote.inicio_operacion) / 3.6e6 : null,
          min_sin_datos: activo && min !== null ? Math.round(min * 10) / 10 : 0,
          en_linea: enLinea ? 1 : 0,
          estado_panel: estadoPanel,
          alerta_fin_ciclo: lote && lote.estado === 'fin_ciclo' ? 1 : 0,
          alerta_modo_seguro: lote && lote.estado === 'modo_seguro' ? 1 : 0,
        });
      }
      return json(200, filas);
    },

    async 'GET /api/eventos'(q) {
      const filtro = {};
      if (q.lote) filtro.lote = q.lote;
      if (q.eq) filtro.eq = q.eq;
      const lim = Math.min(Number(q.limite) || 1000, 5000);
      const e = await (await base()).collection('eventos').find(filtro, { projection: { _id: 0 } }).sort({ ts: -1 }).limit(lim).toArray();
      return json(200, e.map((x) => ({ ...x, datos: JSON.stringify(x.datos) })));
    },

    async 'GET /api/rechazados'(q) {
      const filtro = { recibido: { $gte: fecha(q.desde, new Date(0)), $lte: fecha(q.hasta, new Date(8.64e15)) } };
      const r = await (await base()).collection('rechazados').find(filtro, { projection: { _id: 0, payload: 0 } }).sort({ recibido: -1 }).limit(1000).toArray();
      return json(200, r);
    },

    async 'GET /api/export.csv'(q) {
      if (!q.lote) return json(400, { error: 'falta lote' });
      const d = await base();
      const lote = await d.collection('lotes').findOne({ _id: q.lote });
      const m = await d.collection('mediciones').find({ 'meta.lote': q.lote }).sort({ ts: 1 }).toArray();
      const cols = ['ts', 'seq', 'estado', 'pausa', ...CAMPOS_REGISTRO, ...CAMPOS_DERIVADOS, 'm_obj', 'setpoint'];
      return {
        status: 200,
        headers: { 'content-type': 'text/csv; charset=utf-8', 'content-disposition': `attachment; filename="${q.lote}.csv"` },
        body: aCsv(m.map((x) => fila(x, lote)), cols),
      };
    },

    async 'POST /api/lotes/:id/config'(q, body, params) {
      const err = validarConfig(body || {});
      if (err) return json(400, { error: err });
      const lotes = (await base()).collection('lotes');
      const lote = await lotes.findOne({ _id: params.id });
      if (!lote) return json(404, { error: 'lote inexistente' });
      const c = {};
      for (const k of ['x0', 'xf', 'setpoint', 'intervalo_s']) if (body[k] !== undefined) c[k] = body[k];
      if (c.x0 !== undefined) c.x0_lab = false;   // un x0 escrito a mano es provisional
      Object.assign(lote, c);
      if (esNum(lote.m0) && esNum(lote.x0) && esNum(lote.xf)) c.m_obj = lote.m_obj = masaObjetivo(lote.m0, lote.x0, lote.xf);
      await lotes.updateOne({ _id: lote._id }, { $set: c });
      return { ...json(200, { ok: true, lote: lote._id, config: c }), mqtt: await publicarConfig(lote) };
    },

    async 'POST /api/lotes/:id/gravimetria/inicial'(q, body, params) {
      const err = !body || body.x0 === undefined ? 'falta x0' : validarConfig({ x0: body.x0 });
      if (err) return json(400, { error: err });
      const lotes = (await base()).collection('lotes');
      const lote = await lotes.findOne({ _id: params.id });
      if (!lote) return json(404, { error: 'lote inexistente' });
      const c = { x0: body.x0, x0_lab: true, gravimetria_inicial: { x0: body.x0, registrada: ahora() } };
      Object.assign(lote, c);
      if (esNum(lote.m0) && esNum(lote.xf)) c.m_obj = lote.m_obj = masaObjetivo(lote.m0, lote.x0, lote.xf);
      await lotes.updateOne({ _id: lote._id }, { $set: c });
      return { ...json(200, { ok: true, lote: lote._id, x0: lote.x0, m_obj: lote.m_obj ?? null }), mqtt: await publicarConfig(lote) };
    },

    async 'POST /api/lotes/:id/gravimetria/final'(q, body, params) {
      if (!body || !esNum(body.xf_medida) || body.xf_medida <= 0 || body.xf_medida >= 1)
        return json(400, { error: 'xf_medida debe ser una fracción entre 0 y 1' });
      const r = await (await base()).collection('lotes').updateOne(
        { _id: params.id }, { $set: { gravimetria_final: { xf_medida: body.xf_medida, registrada: ahora() } } });
      if (!r.matchedCount) return json(404, { error: 'lote inexistente' });
      return json(200, { ok: true });
    },
  };

  // Punto de entrada para los nodos http in. `ruta` es "MÉTODO /patrón".
  async function http(ruta, req) {
    const rol = rolDe(req);
    if (!rol) {
      // La página /app pide la clave en su propio formulario: sin este encabezado el navegador
      // mostraría además su ventana de usuario y clave.
      const pagina = req && req.headers && req.headers['x-requested-with'] === 'secador-app';
      return { status: 401, headers: pagina ? {} : { 'www-authenticate': 'Basic realm="secador"' }, body: { error: 'no autorizado' } };
    }
    if (ruta === 'GET /api/yo') return json(200, { rol });
    if (ruta.startsWith('POST') && rol !== 'tecnico') return json(403, { error: 'solo el usuario tecnico' });
    const f = rutas[ruta];
    if (!f) return json(404, { error: 'ruta desconocida' });
    try {
      return await f(req.query || {}, req.body, req.params || {});
    } catch (e) {
      console.error(ruta, e);
      return json(500, { error: 'error interno' });
    }
  }

  async function cerrar() {
    for (const t of vigilancia.values()) clearTimeout(t);
    if (cliente) await cliente.close();
  }

  return {
    base, http, cerrar, stats,
    ingresarTelemetria: enOrden(ingresarTelemetria),
    ingresarEvento: enOrden(ingresarEvento),
  };
}

module.exports = {
  crear, eqDelTopico, validarRegistro, validarEvento, validarConfig, interpolar, derivadosFijos, derivadosLote,
  masaObjetivo, aCsv, CALIBRACION_INICIAL,
};
