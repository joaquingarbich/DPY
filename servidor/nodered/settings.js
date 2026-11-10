// Configuración de Node-RED para el secador. Todo lo variable viene del entorno (.env).
'use strict';
const path = require('path');

const env = (k, def) => process.env[k] ?? def;
// Con `npm run servidor` (sin Docker) solo está MQTT_HOST: se arma la URL como en docker-compose.
if (!process.env.MQTT_URL && process.env.MQTT_HOST) process.env.MQTT_URL = `mqtts://${process.env.MQTT_HOST}:8883`;
// En .env el hash se escribe con $$ para Docker Compose; fuera de Docker llega tal cual.
const hashEditor = env('NODE_RED_EDITOR_HASH', '').replace(/\$\$/g, '$');
const secador = require('./lib/secador').crear({
  uri: env('MONGODB_URI', 'mongodb://localhost:27017'),
  db: env('MONGODB_DB', 'secador'),
  usuarios: { tecnico: env('API_TECNICO_CLAVE'), grafana: env('API_GRAFANA_CLAVE'), operario: env('API_OPERARIO_CLAVE') },
});

module.exports = {
  uiPort: Number(env('PORT', 1880)),
  flowFile: path.join(__dirname, 'flows.json'),
  // flows_cred.json solo contiene ${MQTT_USUARIO} y ${MQTT_CLAVE}: los valores vienen del entorno.
  credentialSecret: false,
  // Sin hash el editor queda DESACTIVADO (falla cerrado). En desarrollo, NODE_RED_EDITOR_ABIERTO=1 lo abre sin clave.
  httpAdminRoot: hashEditor || env('NODE_RED_EDITOR_ABIERTO') === '1' ? '/editor' : false,
  httpNodeRoot: '/',
  // Página de operación (vista del operario y acciones del técnico) en /app/
  httpStatic: [{ path: path.join(__dirname, 'publico'), root: '/app/' }],
  // Hash bcrypt de la clave del editor: `npx node-red admin hash-pw`.
  adminAuth: hashEditor ? {
    type: 'credentials',
    users: [{ username: 'admin', password: hashEditor, permissions: '*' }],
  } : undefined,
  functionGlobalContext: { secador },
  logging: { console: { level: 'info', metrics: false, audit: false } },
  editorTheme: { projects: { enabled: false }, tours: false },
  diagnostics: { enabled: false },
  telemetry: { enabled: false },
};
