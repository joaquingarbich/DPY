# Secador solar de mango: firmware y servidor

Software del proyecto *Optimización del secado solar de mango a través de un colector térmico y trazabilidad de variables de secado en la región Las Lomas, Piura*.

Un ESP32 en el secador mide la masa del mango, las temperaturas, las humedades y la radiación. Controla el ventilador y avisa cuando el mango llegó a la humedad final. Los datos van a un servidor en la nube, donde se ven desde el celular y quedan guardados para el informe.

---

## Cómo funciona

```
 SECADOR (Las Lomas)                          NUBE
 ┌──────────────────────────┐   WiFi + MQTT   ┌──────────────┐   ┌──────────────────────┐
 │ ESP32  ── sensores       │ ──────────────► │ HiveMQ Cloud │◄─►│ Node-RED             │
 │   │     (masa, T, HR,    │   telemetría    │ (broker)     │   │  valida, calcula,    │
 │   │      luz)            │   eventos       └──────────────┘   │  guarda, API, /app   │
 │   ├── ventilador (PI)    │ ◄────────────── config                └──────┬───────────────┘
 │   ├── botón + LED        │                                            │
 │   └── microSD (respaldo) │                                   ┌────────▼────────┐
 └──────────────────────────┘                                   │ MongoDB Atlas   │
                                                                └────────┬────────┘
       ┌────────────────────────────┐                                     │ API
       │ Página /app/ (celular)     │◄────────────────────────────────────┤
       │  operario: estado y avance │                                     │
       │  técnico: gravimetría,     │                           ┌─────────▼─────────┐
       │  setpoint, CSV             │                           │ Grafana           │──► Telegram
       └────────────────────────────┘                           │ gráficos, alertas │   (alertas)
                                                                └───────────────────┘
```

### Ciclo de un lote

1. **Tara.** Con la bandeja vacía, el operario pulsa el botón del secador (corto). El equipo pesa la bandeja y crea el lote (`L-05`). El servidor lo registra solo.
2. **Carga.** Pone el mango y mantiene el botón 3 s. El equipo guarda la masa inicial y empieza a secar:
   - Cada minuto lee los sensores, balanza incluida. Un controlador PI ajusta el ventilador para mantener la cámara en el setpoint (60 °C).
   - Cada minuto guarda un registro en la microSD y lo envía al servidor. Si no hay red, lo reenvía al reconectarse.
   - De noche (sin sol) entra en pausa y apaga el ventilador.
   - Si falla el sensor de la cámara, entra en modo seguro con el ventilador al máximo.
3. **Gravimetría.** Unas 7 h después, el técnico carga en `/app/` la humedad inicial medida en el laboratorio. El servidor la envía al equipo, que recalcula la masa objetivo: la masa a la que el mango tiene la humedad final deseada (17 %).
4. **Fin de ciclo.** Cuando la masa baja de la objetivo en 3 registros seguidos, el equipo avisa. Llega un Telegram ("retire la bandeja") y `/app/` se pone naranja.
5. **Cierre.** El operario retira la bandeja y mantiene el botón 3 s. El lote queda cerrado y sus datos se pueden exportar en CSV.

---

## Qué hay en el repositorio

| Carpeta / archivo | Qué es |
|---|---|
| [`Documentacion_firmware_y_servidor_secador.md`](Documentacion_firmware_y_servidor_secador.md) | **Diseño completo** (pines, tareas, control, estados, mensajes, servidor). Se actualiza antes que el código. La §18 lista los cambios hechos al escribir el código |
| [`GUIA_PUESTA_EN_MARCHA.md`](GUIA_PUESTA_EN_MARCHA.md) | **Por dónde empezar**: de este repositorio a un prototipo enviando datos a la nube, por fases |
| [`MANUAL_OPERACION.md`](MANUAL_OPERACION.md) | **Uso diario** para el operario y el técnico |
| [`hardware/CONEXIONES.md`](hardware/CONEXIONES.md) | **Conexiones** de sensores, microSD, ventilador y alimentación, con diagrama y lista de materiales |
| [`firmware/`](firmware/) | Programa del ESP32 (PlatformIO). [`firmware/MANUAL.md`](firmware/MANUAL.md): cómo compilarlo, cargarlo y calibrar |
| [`servidor/`](servidor/) | Node-RED, Grafana, página `/app/` y despliegue. [`servidor/MANUAL_DESPLIEGUE.md`](servidor/MANUAL_DESPLIEGUE.md): cómo probarlo en local y subirlo a la nube |

### Firmware (`firmware/`)

| Módulo | Función |
|---|---|
| `sensores`, `celda` | SHT31 ×2, DS18B20, BH1750 y HX711; rangos válidos; tara y corrección térmica de la celda |
| `filtros` | Mediana y media que ignoran lecturas inválidas |
| `control_pi`, `ventilador` | PI con anti-windup, PWM a 25 kHz, interruptor y tacómetro |
| `estados`, `fin_ciclo` | Máquina de estados (espera, carga, operación, fin de ciclo, modo seguro), pausa nocturna, masa objetivo |
| `registro_json`, `almacenamiento` | Registro de 10 min en JSON; microSD con reenvío de pendientes |
| `enlace`, `mqtt`, `reloj` | WiFi (reemplazable por celular), MQTT con TLS, hora por DS3231 y NTP |
| `persistencia`, `boton`, `diagnostico`, `main` | Memoria NVS, botón, LED, consola serie y las cuatro tareas de FreeRTOS |

### Servidor (`servidor/`)

| Parte | Función |
|---|---|
| `nodered/lib/secador.js` | Toda la lógica: validar, deduplicar, calcular derivados (humedad, MR, avance, eficiencia), ciclo de vida del lote, API |
| `nodered/flows.json` | Conecta MQTT y HTTP con la lógica |
| `nodered/publico/index.html` | Página `/app/`: vista del operario y acciones del técnico |
| `grafana/` | Tableros "Operario" y "Técnico", alertas a Telegram y script de usuarios |
| `docker-compose.yml`, `Caddyfile` | Despliegue en una máquina virtual con HTTPS |
| `dev/` | Entorno local sin cuentas y simulador del ESP32 |

---

## Usuarios y accesos

Las claves **no están en el repositorio**. Están en `servidor/.env` y en `firmware/include/secretos.h`, que solo existen en la computadora de quien despliega. Pídaselas al responsable del proyecto.

| Usuario | Dónde se usa | Rol |
|---|---|---|
| `operario` | Página `/app/` y Grafana | Solo ve el estado del secador |
| `tecnico` | Página `/app/` y Grafana | Ve todo; carga gravimetría, cambia setpoint e intervalo, exporta CSV |
| `admin` (Grafana) | Grafana | Administra Grafana |
| `admin` (Node-RED) | Editor `/editor` | Modifica los flujos (solo para depurar) |
| `grafana` | Interno: Grafana → API | Solo lectura |
| `sec-01` | ESP32 → HiveMQ | Publica los datos del secador |
| `servidor` | Node-RED → HiveMQ | Recibe datos y envía configuración |
| `secador` | Node-RED → MongoDB Atlas | Lectura y escritura en la base `secador` |

---

## Estado

| Parte | Estado |
|---|---|
| Firmware | Compila para el ESP32; **27 pruebas** pasan en la computadora. **Falta probarlo en la placa** (ver `firmware/MANUAL.md`) |
| Servidor | **25 pruebas** pasan; probado de punta a punta en local con el simulador. **Falta desplegarlo** (cuentas, máquina virtual, dominio) |
| Calibración | La curva de caudal es de ejemplo: la eficiencia η no vale para el informe hasta cargar la medida |

### Probar en una computadora, sin hardware ni cuentas

```
cd servidor && npm install
npm test          # pruebas del servidor
npm run dev       # MongoDB, broker, Node-RED y Grafana locales
npm run simular   # (otra terminal) un lote simulado
```

### Conectar con la nube real

```
npm run verificar-nube                 # crea la base en Atlas si no existe y prueba HiveMQ (usa servidor/.env)
npm run verificar-nube -- --escuchar 120   # además muestra lo que envía la placa
npm run servidor                       # el servidor real en esta computadora, conectado a Atlas y HiveMQ
```

Abra http://localhost:1880/app/ (usuarios `operario` / `operario-dev`, `tecnico` / `tecnico-dev`) y http://localhost:3000 (Grafana, `admin` / `admin`).
