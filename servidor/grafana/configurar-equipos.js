// Crea los usuarios y equipos de Grafana y los permisos de las carpetas (§15.4).
// Grafana no permite aprovisionarlos con archivos, así que se hace por su API.
// Se puede correr varias veces.
//   GRAFANA_URL=https://secador.ejemplo.pe GF_ADMIN_CLAVE=... \
//   OPERARIO_CLAVE=... TECNICO_CLAVE=... node grafana/configurar-equipos.js
'use strict';
const URL_G = process.env.GRAFANA_URL || 'http://localhost:3000';
const AUTH = 'Basic ' + Buffer.from(`admin:${process.env.GF_ADMIN_CLAVE || 'admin'}`).toString('base64');

async function api(metodo, ruta, cuerpo) {
  const r = await fetch(URL_G + ruta, {
    method: metodo,
    headers: { authorization: AUTH, 'content-type': 'application/json' },
    body: cuerpo ? JSON.stringify(cuerpo) : undefined,
  });
  const t = await r.text();
  if (!r.ok && r.status !== 409 && r.status !== 412) throw new Error(`${metodo} ${ruta}: ${r.status} ${t}`);
  return t ? JSON.parse(t) : {};
}

async function usuario(login, clave, rol) {
  if (!clave) throw new Error(`falta la clave de ${login}`);
  let u = await api('GET', `/api/users/lookup?loginOrEmail=${login}`).catch(() => null);
  if (!u || !u.id) u = { id: (await api('POST', '/api/admin/users', { name: login, login, email: `${login}@secador.local`, password: clave })).id };
  await api('PATCH', `/api/org/users/${u.id}`, { role: rol });
  return u.id;
}

async function equipo(nombre, miembro) {
  const b = await api('GET', `/api/teams/search?name=${nombre}`);
  const id = b.teams && b.teams.length ? b.teams[0].id : (await api('POST', '/api/teams', { name: nombre })).teamId;
  await api('POST', `/api/teams/${id}/members`, { userId: miembro });   // 400/409 si ya es miembro
  return id;
}

(async () => {
  const op = await usuario('operario', process.env.OPERARIO_CLAVE, 'Viewer');
  const tec = await usuario('tecnico', process.env.TECNICO_CLAVE, 'Editor');
  const eOp = await equipo('operario', op).catch((e) => { if (/already|400/.test(e.message)) return null; throw e; });
  const eTec = await equipo('tecnico', tec).catch((e) => { if (/already|400/.test(e.message)) return null; throw e; });
  const ids = async (n) => (await api('GET', `/api/teams/search?name=${n}`)).teams[0].id;
  const tOp = eOp || await ids('operario'), tTec = eTec || await ids('tecnico');

  // Reemplaza los permisos por defecto: el operario solo ve su tablero.
  await api('POST', '/api/folders/operario/permissions', { items: [{ teamId: tOp, permission: 1 }, { teamId: tTec, permission: 1 }] });
  await api('POST', '/api/folders/tecnico/permissions', { items: [{ teamId: tTec, permission: 2 }] });
  console.log('listo: usuarios operario (Viewer) y tecnico (Editor), equipos y permisos de carpetas');
})().catch((e) => { console.error(e.message); process.exit(1); });
