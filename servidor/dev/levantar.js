// Levanta todo en local, sin Docker ni cuentas: MongoDB en memoria, broker MQTT (aedes),
// Node-RED y, si está instalado, Grafana. Ctrl+C para detener.
'use strict';
const net = require('net');
const path = require('path');
const { spawn, execSync } = require('child_process');
const fs = require('fs');
const { MongoMemoryServer } = require('mongodb-memory-server');
const { Aedes } = require('aedes');

const RAIZ = path.join(__dirname, '..');
const DEV = {
  MQTT_USUARIO: 'sec-01', MQTT_CLAVE: 'clave-dev',
  API_TECNICO_CLAVE: 'tecnico-dev', API_GRAFANA_CLAVE: 'grafana-dev',
  GF_ADMIN_CLAVE: 'admin',
  TELEGRAM_BOT_TOKEN: '0:token-de-prueba', TELEGRAM_CHAT_OPERARIO: '1', TELEGRAM_CHAT_GRUPO: '2',
};
const hijos = [];

function lanzar(nombre, cmd, args, env) {
  const p = spawn(cmd, args, { env: { ...process.env, ...env }, stdio: ['ignore', 'pipe', 'pipe'] });
  const pre = (d) => String(d).trimEnd().split('\n').map((l) => `[${nombre}] ${l}`).join('\n');
  p.stdout.on('data', (d) => console.log(pre(d)));
  p.stderr.on('data', (d) => console.error(pre(d)));
  hijos.push(p);
  return p;
}

(async () => {
  const mongo = await MongoMemoryServer.create({ binary: { version: '8.0.4' }, instance: { port: 27017 } });
  console.log(`[mongo] ${mongo.getUri()}`);

  const broker = await Aedes.createBroker({
    authenticate: (c, u, p, cb) => cb(null, u === DEV.MQTT_USUARIO && String(p) === DEV.MQTT_CLAVE),
  });
  broker.on('publish', (pk, c) => { if (c) console.log(`[mqtt] ${pk.topic} ${String(pk.payload).slice(0, 120)}`); });
  await new Promise((r) => net.createServer(broker.handle).listen(1883, r));
  console.log('[mqtt] mqtt://localhost:1883 (usuario sec-01)');

  lanzar('node-red', process.execPath, [path.join(RAIZ, 'node_modules/node-red/red.js'), '--settings', path.join(RAIZ, 'nodered/settings.js'), '--userDir', path.join(__dirname, '.nodered')], {
    ...DEV, MONGODB_URI: mongo.getUri(), MQTT_URL: 'mqtt://localhost:1883',
  });

  let grafana = null;
  try { grafana = execSync('command -v grafana', { encoding: 'utf8' }).trim(); } catch {}
  if (grafana) {
    const home = execSync('brew --prefix', { encoding: 'utf8' }).trim() + '/share/grafana';
    const datos = path.join(__dirname, '.grafana');
    const plugins = path.join(datos, 'plugins');
    if (!fs.existsSync(path.join(plugins, 'yesoreyeram-infinity-datasource')))
      execSync(`grafana cli --homepath "${home}" --pluginsDir "${plugins}" plugins install yesoreyeram-infinity-datasource`, { stdio: 'inherit' });
    lanzar('grafana', grafana, ['server', '--homepath', home], {
      ...DEV,
      GF_PATHS_DATA: datos, GF_PATHS_PLUGINS: plugins, GF_PATHS_LOGS: path.join(datos, 'logs'),
      GF_PATHS_PROVISIONING: path.join(RAIZ, 'grafana/provisioning'),
      GF_SECURITY_ADMIN_PASSWORD: DEV.GF_ADMIN_CLAVE, GF_LOG_LEVEL: 'warn',
      NODERED_URL: 'http://localhost:1880', GRAFANA_DASHBOARDS: path.join(RAIZ, 'grafana/dashboards'),
    });
    console.log('[grafana] http://localhost:3000 (admin / admin)');
  } else console.log('[grafana] no instalado: brew install grafana');

  const salir = async () => { hijos.forEach((h) => h.kill()); await mongo.stop(); process.exit(0); };
  process.on('SIGINT', salir);
  process.on('SIGTERM', salir);
})();
