// Comprueba las cuentas en la nube con los datos de servidor/.env:
//   1. MongoDB Atlas: se conecta y CREA la base si no existe (colecciones, índices y
//      calibración inicial; es lo mismo que hace Node-RED al arrancar).
//   2. HiveMQ Cloud: se conecta con el usuario del servidor y publica/recibe un mensaje.
//   3. Con --escuchar N: muestra durante N segundos todo lo que llega a secador/# (la placa).
// Uso:  npm run verificar-nube                 npm run verificar-nube -- --escuchar 120
'use strict';
const mqtt = require('mqtt');
const { crear } = require('../nodered/lib/secador');

const i = process.argv.indexOf('--escuchar');
const ESCUCHAR = i > 0 ? Number(process.argv[i + 1] || 60) : 0;
const ok = (m) => console.log('  ✔ ' + m), mal = (m) => console.log('  ✘ ' + m), nota = (m) => console.log('    ' + m);
let fallas = 0;

async function atlas() {
  console.log('\n1. MongoDB Atlas');
  const uri = process.env.MONGODB_URI;
  if (!uri || uri.includes('COMPLETAR')) { fallas++; return mal('Falta MONGODB_URI en .env (complete el host del clúster).'); }
  const srv = crear({ uri, db: process.env.MONGODB_DB || 'secador' });
  try {
    const db = await Promise.race([srv.base(), new Promise((_, r) => setTimeout(() => r(new Error('tiempo agotado (20 s)')), 20000))]);
    ok(`Conectado. Base "${db.databaseName}" lista (creada si no existía).`);
    for (const c of ['mediciones', 'eventos', 'lotes', 'rechazados']) nota(`${c.padEnd(11)} ${await db.collection(c).estimatedDocumentCount()} documentos`);
    const cal = await db.collection('calibracion').findOne({ _id: 'actual' });
    nota(`calibracion  S=${cal.S}  C_T=${cal.C_T}  K=${cal.K}${cal.provisional ? '  (curva de caudal PROVISIONAL)' : ''}`);
    const ult = await db.collection('mediciones').find().sort({ ts: -1 }).limit(1).next();
    if (ult) nota(`último registro: ${ult.meta.eq} ${ult.meta.lote} seq ${ult.seq} a las ${ult.ts.toISOString()}`);
  } catch (e) {
    fallas++;
    mal(`No se pudo: ${e.message}`);
    if (/auth|Authentication/i.test(e.message)) nota('→ Usuario o clave de MONGODB_URI incorrectos (Atlas → Database Access).');
    else if (/timed out|ENOTFOUND|tiempo agotado|Server selection/i.test(e.message)) nota('→ Revise Atlas → Network Access: agregue la IP de esta computadora o servidor.');
  } finally { await srv.cerrar().catch(() => {}); }
}

async function hivemq() {
  console.log('\n2. HiveMQ Cloud');
  const url = process.env.MQTT_URL || (process.env.MQTT_HOST && `mqtts://${process.env.MQTT_HOST}:8883`);
  if (!url || /x{6}/.test(url)) { fallas++; return mal('Falta MQTT_HOST en .env.'); }
  const c = mqtt.connect(url, {
    username: process.env.MQTT_USUARIO, password: process.env.MQTT_CLAVE,
    clientId: 'verificar-' + Math.random().toString(16).slice(2, 8), reconnectPeriod: 0, connectTimeout: 15000,
  });
  const conectado = await new Promise((res) => { c.once('connect', () => res(true)); c.once('error', (e) => { mal(e.message); res(false); }); });
  if (!conectado) { fallas++; nota('→ Revise MQTT_HOST, MQTT_USUARIO y MQTT_CLAVE (HiveMQ → Access Management).'); c.end(true); return; }
  ok(`Conectado a ${url} como "${process.env.MQTT_USUARIO}".`);
  const vistos = {};
  c.on('message', (t, m) => {
    vistos[t] = (vistos[t] || 0) + 1;
    if (ESCUCHAR && !t.endsWith('/_verificar')) console.log(`    ${new Date().toLocaleTimeString()}  ${t}  ${String(m).slice(0, 140)}`);
  });
  await new Promise((r) => c.subscribe('secador/#', { qos: 1 }, r));
  await new Promise((r) => c.publish('secador/_verificar', 'ping', { qos: 1 }, r));
  await new Promise((r) => setTimeout(r, 3000));
  if (vistos['secador/_verificar']) ok('Publicar y recibir funciona.');
  else { fallas++; mal('Se publicó pero no volvió el mensaje: revise los permisos del usuario en HiveMQ.'); }
  const config = Object.keys(vistos).filter((t) => t.endsWith('/config'));
  if (config.length) nota(`Configuración retenida en: ${config.join(', ')}`);
  if (ESCUCHAR) {
    console.log(`\n3. Escuchando secador/# durante ${ESCUCHAR} s (encienda la placa)…`);
    await new Promise((r) => setTimeout(r, ESCUCHAR * 1000));
    const de = Object.entries(vistos).filter(([t]) => !t.endsWith('/_verificar') && !t.endsWith('/config'));
    if (de.length) ok('Llegaron mensajes: ' + de.map(([t, n]) => `${t} ×${n}`).join(', '));
    else { mal('No llegó nada de la placa.'); nota('→ Revise el monitor serie de la placa: WiFi, MQTT_HOST y la clave de sec-01 en secretos.h. Recuerde: sin lote activo solo se publica "estado".'); }
  }
  c.end(true);
}

(async () => {
  console.log('Verificación de la nube del secador (datos de servidor/.env)');
  await atlas();
  await hivemq();
  console.log(fallas ? `\n${fallas} problema(s). Corrija y vuelva a correr.` : '\nTodo en orden.');
  process.exit(fallas ? 1 : 0);
})();
