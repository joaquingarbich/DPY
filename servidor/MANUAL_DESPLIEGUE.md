# Manual de despliegue del servidor del secador

Para dejar en la nube el servidor descrito en el §15 de `../Documentacion_firmware_y_servidor_secador.md`: Node-RED, MongoDB Atlas, HiveMQ Cloud y Grafana con alertas por Telegram. Siga las partes en orden. La parte 1 se hace en su computadora y no necesita ninguna cuenta.

---

## Parte 1. Probar en su computadora (sin cuentas)

Necesita **Node.js 20 o más** (https://nodejs.org). Grafana es opcional para esta parte: en macOS, `brew install grafana`.

```
cd servidor
npm install
npm test            # 23 pruebas, con un MongoDB real en memoria
npm run dev         # levanta MongoDB, broker MQTT, Node-RED y Grafana
```

En otra terminal:

```
npm run simular     # publica 20 h de un lote L-04 como si fuera el ESP32
```

| Qué | Dónde | Usuario / clave |
|---|---|---|
| Grafana | http://localhost:3000 | `admin` / `admin` |
| Editor de Node-RED | http://localhost:1880/editor | sin clave en desarrollo |
| API | http://localhost:1880/api/... | `grafana` / `grafana-dev`, `tecnico` / `tecnico-dev` |

Para probar la gravimetría y ver aparecer el avance y la humedad en los tableros:

```
curl -u tecnico:tecnico-dev -H 'content-type: application/json' -d '{"x0":0.8281}' \
  http://localhost:1880/api/lotes/L-04/gravimetria/inicial
```

Las alertas se ven en Grafana → *Alerting → Alert rules*. El envío a Telegram falla en local porque el token es de prueba, y es lo esperado. Los datos viven en memoria: se pierden al cerrar `npm run dev`.

---

## Parte 2. Crear las cuentas

### 2.1 HiveMQ Cloud (broker MQTT)

1. Regístrese en https://console.hivemq.cloud y cree un clúster **Serverless (Free)**.
2. En *Overview*, copie la **Cluster URL**, por ejemplo `abc123.s1.eu.hivemq.cloud`.
3. En *Access Management → Credentials*, cree **dos** usuarios:
   - `sec-01`: para el ESP32. Va en `firmware/include/secretos.h`.
   - `servidor`: para Node-RED. Va en `servidor/.env`.

### 2.2 MongoDB Atlas (base de datos)

1. Regístrese en https://cloud.mongodb.com y cree un clúster **M0 (Free)**.
2. En *Database Access*, cree un usuario con el rol **readWrite** sobre la base `secador`.
3. En *Network Access*, agregue la IP pública de la máquina virtual (parte 2.3).
4. En *Connect → Drivers*, copie la cadena `mongodb+srv://…` y ponga su usuario y clave.

No hace falta crear colecciones: Node-RED las crea al arrancar.

### 2.3 Máquina virtual

Cualquier proveedor sirve (Oracle Cloud Free Tier, AWS Lightsail, DigitalOcean, etc.) con **Ubuntu 24.04**, 1 vCPU y 1 GB de RAM.
- Abra los puertos **80** y **443** en el firewall del proveedor.
- Con 1 GB de RAM, cree 1 GB de *swap*:

```
sudo fallocate -l 1G /swapfile && sudo chmod 600 /swapfile && sudo mkswap /swapfile && sudo swapon /swapfile
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

### 2.4 Dominio

Cree un registro **A** (por ejemplo `secador.su-dominio.pe`) que apunte a la IP de la máquina. Un subdominio gratuito de DuckDNS también sirve. Caddy saca el certificado HTTPS solo, pero el dominio tiene que resolver **antes** de levantar los servicios.

### 2.5 Bot de Telegram

1. En Telegram, hable con **@BotFather** → `/newbot` y copie el **token**.
2. El operario le escribe cualquier mensaje al bot. Para el grupo del equipo, agregue el bot al grupo y escriba un mensaje.
3. Abra `https://api.telegram.org/bot<TOKEN>/getUpdates` y copie los `chat.id`. El del grupo empieza con `-`.

---

## Parte 3. Instalar en la máquina virtual

```
# Docker
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker $USER && newgrp docker

# Código
git clone <url-del-repositorio> secador && cd secador/servidor
cp .env.ejemplo .env
nano .env
```

Complete el `.env` con los datos de la parte 2. Use claves largas y distintas para `API_GRAFANA_CLAVE`, `API_TECNICO_CLAVE` y `GF_ADMIN_CLAVE`.

Para proteger el editor de Node-RED:

```
docker compose build nodered
docker compose run --rm nodered npx node-red admin hash-pw
```

Pegue el resultado en `NODE_RED_EDITOR_HASH`, escribiendo **cada `$` como `$$`**.

Levante todo:

```
docker compose up -d --build
docker compose logs -f nodered     # debe decir "Connected to broker"; Ctrl+C para salir
```

### 3.1 Usuarios de Grafana

Desde su computadora (necesita Node.js):

```
cd servidor
GRAFANA_URL=https://secador.su-dominio.pe GF_ADMIN_CLAVE=<la del .env> \
OPERARIO_CLAVE=<clave del operario> TECNICO_CLAVE=<clave del técnico> \
node grafana/configurar-equipos.js
```

Crea los usuarios `operario` (solo ve su tablero) y `tecnico` (ve y edita los dos). Se puede correr de nuevo para cambiar las claves.

---

## Parte 4. Verificar

1. `https://secador.su-dominio.pe` abre Grafana con candado (HTTPS válido).
2. `curl -u grafana:<API_GRAFANA_CLAVE> https://secador.su-dominio.pe/api/lotes` responde `[]`.
3. En Grafana: *Alerting → Contact points → telegram-secador → Test*. El operario y el grupo deben recibir el mensaje.
4. Cargue el firmware con `MQTT_HOST`, `MQTT_USUARIO=sec-01` y `MQTT_CLAVE` en `secretos.h` (ver `firmware/MANUAL.md`).
5. Inicie un lote con el botón. El lote aparece en `/api/lotes` y los datos en "Secador - Técnico" en uno o dos minutos.

---

## Parte 5. Uso diario del técnico

Todos los comandos llevan `-u tecnico:<API_TECNICO_CLAVE>` y `-H 'content-type: application/json'`.

| Qué | Comando |
|---|---|
| Humedad inicial de laboratorio (unas 7 h después del inicio) | `curl … -d '{"x0":0.8281}' https://<dominio>/api/lotes/L-04/gravimetria/inicial` |
| Humedad final medida | `curl … -d '{"xf_medida":0.168}' https://<dominio>/api/lotes/L-04/gravimetria/final` |
| Cambiar setpoint o intervalo | `curl … -d '{"setpoint":58,"intervalo_s":120}' https://<dominio>/api/lotes/L-04/config` |
| Exportar CSV | Enlace del tablero técnico, o `curl … -o L-04.csv 'https://<dominio>/api/export.csv?lote=L-04'` |

La configuración queda **retenida** en el broker: si el equipo está sin red, la recibe al reconectar. Tras aplicarla, el equipo responde con el evento `config_aplicada`, visible en el tablero técnico. Si responde con `config_rechazada`, revise los valores.

**Curva de caudal real.** Cuando la tengan, en Atlas → *Browse Collections* → `secador.calibracion`, edite el documento `actual`: reemplace `curva_caudal` por los puntos medidos (`[{ "duty": 30, "kg_s": … }, …]`) y borre `provisional`. Se aplica en un minuto, sin reiniciar nada.

**Cambios en los flujos o los tableros.** Edite los archivos del repositorio y luego ejecute `git pull && docker compose up -d --build`. Lo que se cambie en el editor de Node-RED de la máquina se pierde al reconstruir. Los tableros se generan con `python3 grafana/generar_tableros.py`.

---

## Problemas frecuentes

| Síntoma | Causa probable | Qué hacer |
|---|---|---|
| Caddy no saca el certificado | El dominio no apunta a la IP, o los puertos 80/443 están cerrados | `docker compose logs caddy`; revisar el registro A y el firewall |
| Node-RED no dice "Connected to broker" | `MQTT_HOST`, usuario o clave mal escritos | Revisar `.env`; probar el usuario `servidor` en el *Web Client* de HiveMQ |
| Node-RED: `MongoServerSelectionError` | La IP de la máquina no está en *Network Access* de Atlas | Agregarla y reiniciar: `docker compose restart nodered` |
| Grafana: paneles con "401" | `API_GRAFANA_CLAVE` cambió y Grafana conserva la anterior | `docker compose up -d --force-recreate grafana` |
| Todo llega a `rechazados` con "lote inexistente" | Se perdió el evento `inicio_lote`/`masa_inicial` del lote | Ver el motivo en el tablero técnico; avisar para recrear el lote |
| No llegan alertas a Telegram | Token o `chat_id` mal escritos, o el operario nunca le escribió al bot | Repetir la parte 2.5 y el *Test* de la parte 4 |
| La η sale rara | La curva de caudal todavía es la de ejemplo | Cargar la curva medida (parte 5) |
