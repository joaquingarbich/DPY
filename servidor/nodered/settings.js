// Configuración de Node-RED para el secador. Todo lo variable viene del entorno (.env).
'use strict';
const path = require('path');

const env = (k, def) => process.env[k] ?? def;
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
  httpAdminRoot: env('NODE_RED_EDITOR_HASH') || env('NODE_RED_EDITOR_ABIERTO') === '1' ? '/editor' : false,
  httpNodeRoot: '/',
  // Página de operación (vista del operario y acciones del técnico) en /app/
  httpStatic: [{ path: path.join(__dirname, 'publico'), root: '/app/' }],
  // Hash bcrypt de la clave del editor: `npx node-red admin hash-pw`.
  adminAuth: env('NODE_RED_EDITOR_HASH') ? {
    type: 'credentials',
    users: [{ username: 'admin', password: env('NODE_RED_EDITOR_HASH'), permissions: '*' }],
  } : undefined,
  functionGlobalContext: { secador },
  logging: { console: { level: 'info', metrics: false, audit: false } },
  editorTheme: { projects: { enabled: false }, tours: false },
  diagnostics: { enabled: false },
  telemetry: { enabled: false },
};
