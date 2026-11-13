# Guía de puesta en marcha

Lista ordenada para pasar de este repositorio a **un prototipo funcionando y enviando datos a la base de datos en la nube**. Cada fase termina con una comprobación: no pase a la siguiente sin ella. Los detalles de cada paso están en los manuales que se enlazan.

| Fase | Resultado | Tiempo aprox. |
|---|---|---|
| 0. Preparación | Repositorio, herramientas y claves | 1 h |
| 1. Cuentas en la nube | Base creada en Atlas y broker probado | 1 h |
| 2. Servidor | Node-RED recibiendo (en una laptop o en la máquina virtual) | 30 min a 2 h |
| 3. Hardware | Prototipo armado y sensores comprobados | 1 a 2 días |
| 4. Firmware | Placa cargada, calibrada y conectada | 2 h |
| 5. Prueba de punta a punta | Un lote de prueba visible en `/app/` y en Atlas | 1 h |
| 6. Campo | Prototipo instalado en el secador | — |

---

## Fase 0. Preparación

- [ ] Clonar el repositorio: `git clone https://github.com/joaquingarbich/DPY.git`
- [ ] Instalar **Node.js 20 o más** (https://nodejs.org) y **VS Code + PlatformIO** (`firmware/MANUAL.md`, sección 1).
- [ ] Conseguir del responsable del proyecto los dos archivos con las claves, que **no están en GitHub**:
  - `servidor/.env` → copiarlo a `servidor/.env`.
  - `firmware/include/secretos.h` → copiarlo a `firmware/include/secretos.h`.
- [ ] `cd servidor && npm install && npm test`: deben pasar las 25 pruebas.

## Fase 1. Cuentas en la nube y creación de la base

Al crear cada usuario, **use la clave que ya está en `servidor/.env` o en `secretos.h`**, no una nueva. Pasos detallados: `servidor/MANUAL_DESPLIEGUE.md`, parte 2.

**MongoDB Atlas (base de datos):**
- [ ] Crear la cuenta y un clúster **M0 (gratis)**. La región más cercana es São Paulo (`sa-east-1`) en AWS.
- [ ] *Database Access → Add New Database User*:
  - Usuario `secador`.
  - Clave: la que está en `MONGODB_URI` del `.env`, entre `secador:` y `@`.
  - Rol: *Read and write to any database*. Si quiere restringirlo, use *Specific privileges* con `readWrite` sobre `secador`.
- [ ] *Network Access → Add IP Address*: la IP de la computadora o del servidor que se conectará. Para las primeras pruebas puede usar *Allow access from anywhere* (`0.0.0.0/0`); **quítelo** cuando el servidor tenga IP fija.
- [ ] *Connect → Drivers*: copiar el host del clúster (por ejemplo `cluster0.ab1cd.mongodb.net`) y reemplazar `COMPLETAR.mongodb.net` en `MONGODB_URI` del `.env`.

**HiveMQ Cloud (broker MQTT):**
- [ ] Crear la cuenta y un clúster **Serverless (Free)**. Copiar la *Cluster URL* a `MQTT_HOST` del `.env` **y** a `MQTT_HOST` de `secretos.h`.
- [ ] *Access Management*: crear el usuario `servidor`, con la clave `MQTT_CLAVE` del `.env`, y el usuario `sec-01`, con la clave `MQTT_CLAVE` de `secretos.h`. Ambos con permiso de publicar y suscribirse.

**Crear la base y comprobar todo:**
- [ ] Desde `servidor/`:
  ```
  npm run verificar-nube
  ```
  Esto **crea la base `secador` en Atlas** con sus colecciones (`mediciones`, `eventos`, `lotes`, `calibracion`, `rechazados`), los índices y la calibración inicial, y prueba el broker. Debe terminar en **"Todo en orden"**. Si algo falla, dice qué revisar.
- [ ] En Atlas → *Browse Collections* debe verse la base `secador` con las 5 colecciones.

## Fase 2. Servidor

El servidor tiene que estar corriendo para que los datos de la placa lleguen a la base. Hay dos opciones:

**Opción rápida, para el prototipo: en una laptop.** No necesita máquina virtual ni dominio.
- [ ] Desde `servidor/`:
  ```
  npm run servidor
  ```
  Debe decir `Connected to broker`. Deje esa ventana abierta: mientras corra, todo lo que envíe la placa se guarda en Atlas.
- [ ] Abrir http://localhost:1880/app/ (usuario `tecnico`).
- La laptop tiene que estar encendida y con internet. Si se apaga, la placa guarda los datos en la microSD y los reenvía cuando el servidor vuelve.
- **No la deje corriendo al mismo tiempo que el servidor de la máquina virtual**: los dos usan la misma conexión al broker y se desconectan uno al otro.

**Opción definitiva: en una máquina virtual**, con Grafana, alertas de Telegram y HTTPS.
- [ ] Seguir `servidor/MANUAL_DESPLIEGUE.md`, partes 2.3 a 4. Tiene que agregar la IP de la máquina en Atlas → *Network Access*.

## Fase 3. Hardware

- [ ] Conseguir los materiales de `hardware/CONEXIONES.md`, sección 1.
- [ ] **Ajustar el buck a 5,0 V con un multímetro** antes de conectar el ESP32.
- [ ] Armar **de a un bloque**, comprobando con `estado` (`hardware/CONEXIONES.md`, sección 5).

## Fase 4. Firmware

- [ ] Completar en `firmware/include/secretos.h`: `WIFI_SSID`, `WIFI_CLAVE` y `MQTT_HOST`. La clave de `sec-01` ya está.
- [ ] `pio test -e native` (27 pruebas) y cargar la placa (`firmware/MANUAL.md`, sección 5).
- [ ] Calibrar la celda (`cal tara`, `cal S <g>`) y fijar el contador de lotes (`lote n <último usado>`) (`firmware/MANUAL.md`, sección 6).
- [ ] Comprobar que la placa llega al broker. Con el servidor de la fase 2 corriendo, desde `servidor/`:
  ```
  npm run verificar-nube -- --escuchar 120
  ```
  y reinicie la placa: debe aparecer `secador/SEC-01/estado`. Sin un lote en curso, la placa **solo** publica `estado`; la telemetría empieza con el lote.

## Fase 5. Prueba de punta a punta (en mesa)

- [ ] Iniciar un lote de prueba con una masa conocida, por ejemplo 2 kg:
  - Pulsación corta con la bandeja vacía.
  - Poner la masa, esperar 3 min y hacer una pulsación larga.
- [ ] En `/app/` (usuario `operario`) aparece **"Secando"** y el lote nuevo.
- [ ] Al minuto, en Atlas → `secador.mediciones` aparecen registros con `meta.lote` igual al lote nuevo.
- [ ] Cargar una humedad inicial desde `/app/` → *Acciones del técnico*. En los eventos aparece "Configuración aplicada".
- [ ] **Prueba de corte de red:** apagar el WiFi 30 min y volver a encenderlo. Los registros de esos minutos deben llegar igual, porque salen de la microSD.
- [ ] **Prueba de corte de energía:** desenchufar la placa y volver a enchufarla. Debe aparecer el evento "Reinicio del equipo" y el lote debe seguir.
- [ ] Cerrar el lote con dos pulsaciones largas y descargar el CSV.

## Fase 6. Instalación en campo

- [ ] **Señal de WiFi en el secador:** mírela con `estado` en la consola (`senal`) o en Grafana → *Secador - Técnico* → *Calidad de datos*. Por debajo de unos −80 dBm habrá cortes. En Las Lomas puede hacer falta un router 4G o el punto de acceso de un celular. El firmware ya deja libre la UART2 para un módulo celular a futuro.
- [ ] Caja estanca a la sombra, fuera de la cámara; cables por prensaestopas.
- [ ] Batería y regulador de carga con margen para la noche: el ESP32 y los sensores consumen unos 1,5 W; el ventilador, hasta 2 o 3 W.
- [ ] Bot de Telegram configurado, y el operario ya le escribió (`servidor/MANUAL_DESPLIEGUE.md`, parte 2.5). Probar desde Grafana.
- [ ] Repasar con el operario el `MANUAL_OPERACION.md`.
- [ ] Cuando tengan la curva de caudal medida, cargarla en Atlas (`servidor/MANUAL_DESPLIEGUE.md`, parte 5).

---

### Si algo no llega

Siga el camino de los datos, de la placa a la base:

1. **Placa**: el monitor serie muestra `[evento] …` y `estado` da una `senal` en dBm (no `-128`). Si da `-128`, no hay WiFi.
2. **Broker**: `npm run verificar-nube -- --escuchar 120` muestra los mensajes de `secador/SEC-01/…`. Si no aparecen, revise `MQTT_HOST` y la clave de `sec-01` en `secretos.h`.
3. **Servidor**: la ventana de `npm run servidor` (o `docker compose logs nodered`) no muestra `rechazado: …`. Si dice "lote inexistente", el servidor no estaba corriendo cuando se inició el lote: cierre ese lote y empiece otro.
4. **Base**: `npm run verificar-nube` muestra el "último registro".
