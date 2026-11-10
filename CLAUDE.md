# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Proyecto de tesis (UDEP): secador solar de mango. Un ESP32 mide y controla el secador y envía datos por MQTT. Un servidor (Node-RED, MongoDB Atlas, Grafana y la página `/app/`) los guarda, los muestra y alerta por Telegram. El repositorio está en español: código, comentarios, documentación y mensajes de commit.

## Regla principal

`Documentacion_firmware_y_servidor_secador.md` es la especificación. **Si cambia el comportamiento, se actualiza primero ese documento** (la sección afectada) y se agrega una fila en la tabla de cambios de la §18 (18.1 firmware, 18.2 servidor). Cuando el documento y la realidad no coinciden, se hace el cambio más pequeño que cumpla el documento, se actualiza el `.md` y se registra. Solo se pregunta al usuario si cambia el comportamiento: control, estados, fin de ciclo o formato de mensajes.

## Comandos

### Firmware (`firmware/`, PlatformIO)

PlatformIO está instalado en un venv local: `../.venv-pio/bin/pio`. En otra máquina, `pio` a secas.

```
cd firmware
../.venv-pio/bin/pio test -e native                 # 27 pruebas Unity, sin placa
../.venv-pio/bin/pio test -e native -f test_estados # una sola suite (test_filtros, test_pi, test_fin_ciclo, test_registro, test_estados)
../.venv-pio/bin/pio run -e esp32dev                # compilar para la placa
PLATFORMIO_BUILD_FLAGS="-DMODO_PRUEBAS" ../.venv-pio/bin/pio run -e esp32dev   # con comandos de inyección de fallas
../.venv-pio/bin/pio run -e esp32dev -t upload && ../.venv-pio/bin/pio device monitor
```

Para compilar hace falta `include/secretos.h`, que no se versiona: se copia de `secretos.ejemplo.h`. La plataforma está fijada en `espressif32 @ 8.0.0` (núcleo Arduino 3.3.12 / ESP-IDF 5).

### Servidor (`servidor/`, Node.js ≥ 20)

```
cd servidor
npm test                                         # 25 pruebas node:test con MongoDB 8.0 real en memoria
node --test --test-name-pattern "gravimetría"    # filtrar por nombre
npm run dev        # MongoDB en memoria + broker aedes (1883) + Node-RED (1880) + Grafana (3000, si está instalado con brew)
npm run simular    # otra terminal: simula el ESP32 (lote L-04, 20 h de datos y luego uno por minuto); --horas N --lote L-05 --fin
python3 grafana/generar_tableros.py              # regenera dashboards/*.json (no editar los JSON a mano)
OPERARIO_CLAVE=op-dev-123 TECNICO_CLAVE=tec-dev-123 node grafana/configurar-equipos.js   # usuarios/equipos de Grafana en local
```

URLs locales:
- `/app/` en http://localhost:1880/app/ (`operario`/`operario-dev`, `tecnico`/`tecnico-dev`).
- API en `:1880/api/*` (`grafana`/`grafana-dev`).
- Editor de Node-RED en `:1880/editor`.
- Grafana en `:3000` (`admin`/`admin`).

Los datos de `npm run dev` viven en memoria. Producción usa `docker compose up -d --build` con `servidor/.env`; no se pudo probar en esta Mac porque no hay Docker.

## Arquitectura

### Firmware: lógica pura separada del hardware

- `filtros`, `control_pi`, `fin_ciclo`, `registro_json` y `estados` no incluyen Arduino. Compilan en el entorno `native` (`build_src_filter` en `platformio.ini`) y son lo único que tiene pruebas. Toda lógica nueva que se pueda probar va en estos módulos.
- `estados.cpp` es la máquina de estados (ESPERA → CARGA → OPERACION → FIN_CICLO, más MODO_SEGURO). Recibe una `EntradaControl` y devuelve `SalidaControl`: duty y eventos. **No hace efectos**: `main.cpp::acciones()` aplica cada evento (tara, NVS, microSD, publicar).
- `main.cpp` contiene los cuerpos de las 4 tareas FreeRTOS. Núcleo 1: `tAdquisicion` cada 10 s → notifica a `tControl`; `tRegistro` alineado al minuto UTC. Núcleo 0: `tComunicacion`. El botón y el LED van en un `esp_timer` de 10 ms; la consola serie, en `loop()`. Los recursos compartidos están en `globales.h`, con orden de bloqueo `mtxLectura` → `mtxLote`.
- **La microSD manda.** `tRegistro` escribe cada registro en `/lotes/<lote>.jsonl`. `tComunicacion` **siempre lee de la microSD** (lo nuevo y lo pendiente por el mismo camino), con una ventana de 10 mensajes QoS 1. Con cada PUBACK avanza `/lotes/<lote>.ptr`. `qRegistros` solo se usa si falla la microSD.
- MQTT usa el cliente de ESP-IDF (`mqtt_client.h`), no PubSubClient, porque hacen falta QoS 1, el `msg_id` del PUBACK, TLS y última voluntad. `Enlace` es una interfaz abstracta para poder cambiar WiFi por un módulo celular.
- Solo se registra con lote activo (OPERACION, FIN_CICLO o MODO_SEGURO). MODO_SEGURO no se guarda en NVS.

### Servidor: lógica en un módulo, flujos mínimos

- **Toda la lógica está en `servidor/nodered/lib/secador.js`**: validar, deduplicar, derivados, ciclo de vida del lote por eventos, API y autenticación. `settings.js` lo inyecta en `functionGlobalContext`. Los nodos `function` de `flows.json` son de una o dos líneas que llaman a `global.get('secador')`. Para cambiar comportamiento se edita `secador.js` y se prueba con `npm test`, no en el editor de Node-RED.
- La API se despacha en `secador.http('MÉTODO /ruta/:param', req)`. Para una ruta nueva hay que agregarla al objeto `rutas` **y** un nodo `http in` en `flows.json` que apunte a `f_api`.
- Telemetría y eventos se procesan **de a uno y en orden** (`enOrden`). Sin eso, la primera telemetría de un lote llegaba antes de que `masa_inicial` creara el lote y se rechazaba.
- Los lotes los crea el servidor a partir de los eventos (`inicio_lote`, `masa_inicial`, …). No se crean a mano.
- Derivados: `g_est`, `m_dot` y `eta` se guardan al insertar. `x_bh`, `x_bs`, `mr` y `avance` dependen de `x0` y **se calculan al leer**: son `null` hasta que la gravimetría marca `x0_lab: true`.
- La gravimetría y la configuración publican un mensaje **retenido** en `secador/<eq>/config`, con `x0_lab` (campo agregado al protocolo; ver §11.3).
- Node-RED crea las colecciones de MongoDB al arrancar (`prepararBase()`): serie de tiempo `mediciones`, `eventos`, `lotes`, `calibracion` (curva de caudal **provisional**) y `rechazados`. No hay script de inicialización aparte.
- Usuarios de la API: `grafana` (lectura), `operario` (lectura, página `/app/`) y `tecnico` (además POST). `operario` y `tecnico` usan la misma clave en `/app/` y en Grafana.
- `/app/` es `nodered/publico/index.html`: un solo archivo, sin dependencias. Envía `X-Requested-With: secador-app` para que el 401 no abra la ventana del navegador.
- Grafana lee la API con el complemento Infinity. Las alertas consultan `/api/ultimo` (una fila por equipo, con campos `alerta_*` y `min_sin_datos`).

## Credenciales

- Las reales están **solo** en `servidor/.env` y `firmware/include/secretos.h`, ambos en `.gitignore`. Nunca subirlos.
- En `.env`, cada `$` del hash bcrypt del editor se escribe `$$` (Docker Compose interpola).
- GitHub secret scanning ya marcó una vez una URI de ejemplo `mongodb+srv://usuario:clave@…`. En plantillas y documentación hay que usar `<usuario>:<clave>` o dejar el valor vacío.
- Remoto: https://github.com/joaquingarbich/DPY (público), rama `v1.1.0`. Hay una rama por versión de firmware.

## Documentos

- `README.md`: funcionamiento general.
- `MANUAL_OPERACION.md`: uso diario.
- `firmware/MANUAL.md`: compilar, cargar y calibrar.
- `servidor/MANUAL_DESPLIEGUE.md`: cuentas, máquina virtual y Docker.
- `hardware/conexiones.svg`: diagrama de conexiones.
