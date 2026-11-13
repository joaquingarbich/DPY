# Documentación técnica del firmware y del servidor del secador solar

Proyecto: Optimización del secado solar de mango a través de un colector térmico y trazabilidad de variables de secado en la región Las Lomas, Piura.

Versión del documento: 1.2 (corresponde al firmware `1.1.0`, la versión usada en los ciclos L-02 y L-03). La 1.2 incorpora las decisiones tomadas al escribir el código; ver §18.

El funcionamiento general y el mapa del repositorio están en `README.md`; el uso diario, en `MANUAL_OPERACION.md`.

Este documento reúne todas las decisiones de diseño del software para que el código completo pueda escribirse después y adjuntarse en el apéndice del informe. Los fragmentos de código del apartado 5.2 salen de aquí. Si se cambia algo en el código, se actualiza primero este documento y luego el informe.

---

## 1. Alcance

El software tiene dos partes:

1. **Firmware del nodo** (ESP32 DevKit V1, 38 pines): lee los sensores, filtra, controla el ventilador con un PI, decide el fin de ciclo, guarda todo en la microSD y transmite por MQTT.
2. **Servidor**: broker MQTT en la nube (HiveMQ Cloud), Node-RED para validar y calcular, MongoDB Atlas para almacenar y Grafana para visualizar y alertar.

Para los ensayos el enlace es WiFi. La interfaz de comunicación está separada en una capa `Enlace`, de modo que un módulo celular (SIM800L o LTE por UART) puede reemplazar al WiFi sin tocar el resto del firmware.

---

## 2. Entorno de desarrollo

| Elemento | Valor |
|---|---|
| IDE | Visual Studio Code + PlatformIO |
| Placa | `esp32dev` |
| Framework | Arduino para ESP32 (núcleo 3.3.12, basado en ESP-IDF 5.x), que trae la plataforma `espressif32` 8.0.0 |
| Tabla de particiones | `min_spiffs.csv` (≈1,9 MB para la aplicación; deja espacio para OTA) |
| Velocidad del monitor serie | 115 200 baudios |
| Control de versiones | Git; una rama por versión (`v1.0.0` para L-01, `v1.1.0` para L-02 y L-03) |
| Pruebas unitarias | `pio test -e native` (Unity) para filtros, PI, fin de ciclo, serialización y máquina de estados |

### 2.1 `platformio.ini` de referencia

```ini
[env:esp32dev]
platform = espressif32 @ 8.0.0   ; trae el núcleo Arduino 3.3.12 (ESP-IDF 5.x)
board = esp32dev
framework = arduino
board_build.partitions = min_spiffs.csv
monitor_speed = 115200
build_flags =
    -DFW_VERSION=\"1.1.0\"
    -DCORE_DEBUG_LEVEL=2
    -DSECADOR_ID=\"SEC-01\"
    ; -DMODO_PRUEBAS   ; habilita los comandos de inyección de fallas por serie
lib_deps =
    bblanchon/ArduinoJson @ ^7
    adafruit/Adafruit SHT31 Library
    paulstoffregen/OneWire
    milesburton/DallasTemperature
    claws/BH1750
    bogde/HX711
    adafruit/RTClib

[env:native]
platform = native
test_framework = unity
test_build_src = yes
build_flags = -DFW_VERSION=\"1.1.0\" -DSECADOR_ID=\"SEC-01\"
build_src_filter = +<filtros.cpp> +<control_pi.cpp> +<fin_ciclo.cpp> +<registro_json.cpp> +<estados.cpp>
lib_deps = bblanchon/ArduinoJson @ ^7
```

El cliente MQTT es el de ESP-IDF (`mqtt_client.h`, incluido en el núcleo de Arduino para ESP32). Se eligió porque publica con QoS 1, maneja TLS, permite mensaje de última voluntad y entrega el identificador de cada mensaje confirmado (PUBACK), que el firmware necesita para marcar los registros enviados. Las bibliotecas MQTT más simples para Arduino solo publican con QoS 0.

Las credenciales (SSID, clave WiFi, usuario y clave del broker) van en `include/secretos.h`, que no se sube al repositorio. Se incluye `include/secretos.ejemplo.h` como plantilla, que ya trae el certificado ISRG Root X1.

---

## 3. Asignación de pines

| Señal | GPIO | Notas |
|---|---|---|
| I2C SDA | 21 | SHT31 entrada (0x44), SHT31 cámara (0x45), BH1750 (0x23), DS3231 (0x68). Resistencias de 4,7 kΩ a 3,3 V. Bus a 100 kHz por la longitud del cable a la cámara. |
| I2C SCL | 22 | |
| HX711 DOUT | 34 | Pin solo de entrada |
| HX711 SCK | 25 | |
| DS18B20 (1-Wire) | 4 | Resistencia de 4,7 kΩ a 3,3 V |
| microSD CS | 5 | Bus VSPI |
| microSD MOSI / MISO / SCK | 23 / 19 / 18 | |
| PWM del ventilador | 26 | LEDC canal 0, 25 kHz, 10 bits (especificación de ventiladores de 4 hilos de Intel). Va a la entrada PWM del ventilador axial de 80 mm. |
| Interruptor de alimentación del ventilador | 27 | MOSFET de canal N de nivel lógico en el lado bajo. Apaga el ventilador del todo (un ventilador de 4 hilos con PWM = 0 % sigue girando a baja velocidad). |
| Tacómetro del ventilador | 35 | Entrada con resistencia de 10 kΩ a 3,3 V; 2 pulsos por vuelta |
| Botón de lote | 33 | Pull-up interno, activo en bajo, antirrebote por software (50 ms) |
| LED de estado | 2 | |
| UART2 RX / TX | 16 / 17 | Reservados para el módulo celular (no se usan con WiFi) |

---

## 4. Estructura del proyecto

```
firmware/
├── include/
│   ├── config.h            # constantes y valores por defecto
│   ├── tipos.h             # estructuras compartidas
│   ├── secretos.ejemplo.h
│   ├── globales.h          # recursos compartidos entre tareas
│   └── *.h                 # una cabecera por módulo
├── src/
│   ├── main.cpp            # setup(): crea recursos y tareas; cuerpos de las cuatro tareas
│   ├── sensores.cpp        # drivers y validación de rangos
│   ├── filtros.cpp         # mediana, media de ventana
│   ├── celda.cpp           # tara, factor de escala, corrección térmica
│   ├── control_pi.cpp      # PI con anti-windup
│   ├── estados.cpp         # máquina de estados (sin hardware, probada en native)
│   ├── fin_ciclo.cpp       # masa objetivo y detección
│   ├── ventilador.cpp      # PWM, interruptor, tacómetro
│   ├── registro_json.cpp   # armado del registro de 10 min
│   ├── almacenamiento.cpp  # microSD (JSON Lines) y puntero de pendientes
│   ├── enlace.cpp          # interfaz Enlace + implementación WiFi
│   ├── mqtt.cpp            # cliente esp-mqtt, colas y PUBACK
│   ├── persistencia.cpp    # NVS (Preferences)
│   ├── reloj.cpp           # DS3231 + NTP
│   ├── boton.cpp           # pulsación corta / larga
│   └── diagnostico.cpp     # LED, consola serie, comandos de prueba
└── test/
    ├── test_filtros/
    ├── test_pi/
    ├── test_fin_ciclo/
    ├── test_registro/
    └── test_estados/
```

---

## 5. Estructuras de datos compartidas (`tipos.h`)

```cpp
enum class Estado : uint8_t { ESPERA, CARGA, OPERACION, FIN_CICLO, MODO_SEGURO };

struct Lectura {            // resultado de un ciclo de adquisición (60 s)
  float t_cam, hr_cam;      // SHT31 0x45
  float t_ent, hr_ent;      // SHT31 0x44
  float t_col;              // DS18B20
  float luz_lux;            // BH1750
  float masa_g;             // HX711 corregida (producto, sin tara)
  uint16_t rpm;             // un solo tacómetro (GPIO 35)
  uint8_t flags;            // bit a bit: sensor con error en esta lectura
  uint32_t t_ms;
};

struct ParametrosLote {     // llegan por secador/config; se guardan en NVS
  float x0;                 // humedad inicial b.h. (fracción); 0,83 provisional hasta que llega la de laboratorio
  bool  x0_lab;             // true cuando x0 viene de la gravimetría
  float xf;                 // humedad final objetivo b.h. (fracción)
  float setpoint;           // °C
  uint16_t intervalo_s;     // 60 a 1800
};

struct Lote {
  char id[12];              // "L-02"
  float tara_cuentas;       // lectura bruta de la bandeja vacía
  float m0_g;               // masa inicial confirmada
  float m_obj_g;            // calculada con x0 (provisional o de laboratorio)
  uint32_t seq;             // número del último registro
  uint32_t t_inicio;        // época UTC de masa_inicial (inhibición de 12 h)
  Estado estado;
  bool pausa;
};

struct Registro {           // consolidado del intervalo, 10 min por defecto (lo que se guarda y se envía)
  char ts[21];              // "2026-10-01T19:05:00Z"
  uint32_t seq;
  float masa_g, t_cam, hr_cam, t_ent, hr_ent, t_col, luz_lux, duty;
  Estado estado; bool pausa;
  uint16_t n_descartes;
  int8_t senal_dbm;         // INT8_MIN = sin enlace → null
};
```

Recursos de FreeRTOS:

- `mtxLectura`: mutex que protege la última `Lectura` filtrada.
- `mtxLote`: mutex que protege `Lote` y `ParametrosLote`.
- `qRegistros`: cola de 32 líneas JSON de registro; solo se usa si falla la microSD (§10.2).
- `qEventos`: cola de 32 líneas JSON de eventos hacia la tarea de comunicación.
- `qBoton`: cola de eventos del botón (corta / larga).
- `evAdq`: notificación de tarea de adquisición a control al terminar cada lectura.

---

## 6. Tareas de FreeRTOS

| Tarea | Núcleo | Prioridad | Periodo | Pila (bytes) | Función |
|---|---|---|---|---|---|
| `tAdquisicion` | 1 | 3 | 60 s (`vTaskDelayUntil`, en tramos de 5 s para alimentar el watchdog) | 4096 | Lee los cinco sensores, valida rangos, aplica filtros, corrige la masa por temperatura y publica la `Lectura` filtrada. Notifica a `tControl`. |
| `tControl` | 1 | 4 | Al recibir la notificación (cada 60 s) | 4096 | Atiende el botón, ejecuta la máquina de estados, la lógica de pausa, el PI y el fin de ciclo. Escribe el PWM. |
| `tRegistro` | 1 | 2 | Intervalo de registro (10 min por defecto) alineado a múltiplos del intervalo en UTC | 6144 | Consolida las diez lecturas del intervalo, arma el JSON, lo escribe en la microSD y lo encola para transmisión. |
| `tComunicacion` | 0 | 2 | Bucle con espera de eventos | 8192 | Mantiene WiFi y MQTT, publica la cola, reenvía pendientes de la microSD, recibe `secador/config` y sincroniza la hora por NTP. |

El núcleo 0 queda para la pila WiFi y la tarea de comunicación; las tareas de tiempo real van en el núcleo 1. Así una reconexión lenta no retrasa el control.

Además, un temporizador `esp_timer` de 10 ms atiende el botón y el LED, y la consola serie corre en `loop()` (tarea de Arduino, prioridad 1).

Las cuatro tareas se suscriben al watchdog de tareas (`esp_task_wdt`) con un plazo de 30 s. Cada una lo alimenta al final de su ciclo. `tComunicacion` lo alimenta también dentro de los bucles de reintento.

---

## 7. Adquisición y filtrado

### 7.1 Lectura por sensor (cada 60 s)

Todos los sensores, balanza incluida, se leen una vez por minuto en todos los estados.

| Sensor | Procedimiento | Rango válido | Si falla |
|---|---|---|---|
| SHT31 (×2) | Medición de alta repetibilidad sin *clock stretching*; el CRC lo verifica la biblioteca | T: −10 a 90 °C; HR: 0 a 100 % | `NAN` y bit de error |
| DS18B20 | Conversión asíncrona a 12 bits: se pide al inicio del ciclo y se lee 800 ms después | −10 a 125 °C; descarta 85,0 °C exacto (valor de encendido) | `NAN` |
| BH1750 | Modo de alta resolución, una medición; MTreg por defecto (69) porque el difusor ya evita la saturación | 0 a 65 534 lx; 65 535 se marca como saturado | `NAN` o bandera de saturación |
| HX711 | 8 lecturas a 10 SPS (≈0,8 s); se toma la mediana | ±8 388 607 cuentas; masa −200 a 10 500 g | `NAN` |

Un sensor que devuelve `NAN` tres ciclos seguidos genera el evento `error_sensor` con el nombre del sensor. Para el SHT31 de la cámara, ese mismo criterio lleva a `MODO_SEGURO` (ver §9).

### 7.2 Filtros

- **Masa**: mediana de las 8 muestras de cada lectura (elimina golpes de viento y vibraciones) y, en el registro (10 min por defecto), mediana de las diez lecturas del intervalo.
- **Temperaturas, humedades y luz**: media de las lecturas válidas del intervalo.
- **Descartes**: una lectura de masa que se aleja más de 15 g de la mediana del intervalo se descarta y se cuenta en `n_descartes`, junto con las lecturas `NAN`.
- **Lazo de control**: usa la última temperatura de cámara válida, con mediana de las tres últimas lecturas (3 min).

### 7.3 Celda de carga

```
masa_g = (cuentas − tara_cuentas) / S  +  C_T · (T_cam − 25)
```

- `S` = 211,34 cuentas/g (calibración, §5.3 del informe). Se guarda en NVS (`cal_S`).
- `C_T` = +0,465 g/°C: coeficiente de compensación, con signo opuesto a la pendiente de deriva medida (−0,465 g/°C). Se guarda en NVS (`cal_CT`). La lectura cae al calentarse la celda, por eso el término se suma.
- `tara_cuentas` se toma con la bandeja vacía al pulsar el botón en `ESPERA` (promedio de 20 lecturas estables).
- Estabilidad: una lectura se considera estable si la desviación estándar de las últimas 10 muestras brutas del HX711 es menor de 2 g.
- Tara y `m0` usan el promedio de las últimas 20 muestras brutas, y solo si esas 20 también tienen una desviación menor de 2 g (así no se mezclan muestras de antes de cargar). Como se toman 8 muestras por minuto, las 20 muestras abarcan tres lecturas: en la práctica, la bandeja debe quedar quieta unos 3 min antes de la pulsación.

---

## 8. Control del ventilador

### 8.1 PI discreto

```
e_k = T_cam,k − T_sp                 (acción inversa)
u_k = Kp · e_k + I_k
si 30 < u_k < 100, o (u_k ≥ 100 y e_k < 0), o (u_k ≤ 30 y e_k > 0):
    I_{k+1} = saturar(I_k + Ki · Ts · e_k, 30, 100)   (integración condicional y acotada)
u_k = saturar(Kp · e_k + I_k, 30, 100)
```

| Parámetro | Valor | Origen |
|---|---|---|
| `Kp` | 7,0 %/°C | Sintonía SIMC con τc = 2θ (K = −0,162 °C/%, τ = 20,6 min, θ = 6,0 min) |
| `Ki` | 0,0057 %/(°C·s) (Ti = 20,6 min) | Igual |
| `Ts` | 60 s | Periodo del lazo (menor que θ/5 = 72 s, por eso `Kp` y `Ki` se mantienen) |
| Límites | 30 % a 100 % | Torque mínimo y extracción mínima de aire húmedo |
| `T_sp` por defecto | 60 °C | Centro del rango de trabajo; se cambia por `secador/config` |

Al pasar de ventilador apagado a encendido, el integral se inicializa en 30 % (transferencia sin salto). Al volver de `MODO_SEGURO` se inicializa en el duty que había (100 %). Al cambiar el setpoint no se reinicia el integral.

### 8.2 Pausa por falta de radiación (dentro de `OPERACION`)

- Entra en pausa si `G_est < 120 W/m²` **y** `T_col − T_ent < 3 °C` durante 10 min seguidos.
- Sale de la pausa si `G_est > 180 W/m²` **o** `T_col − T_ent > 6 °C` durante 2 min seguidos.
- En pausa, el interruptor del ventilador se abre y `duty = 0`. Evita gastar batería de noche e impide que entre aire frío y húmedo a la cámara.
- `G_est = luz_lux / K`, con `K` = 46,9 lx/(W/m²) (calibración del BH1750 con difusor). Se guarda en NVS (`cal_K`).

### 8.3 Prioridad de los estados sobre el controlador

| Estado | Ventilador |
|---|---|
| `ESPERA`, `CARGA` | Apagado |
| `OPERACION` en pausa | Apagado |
| `OPERACION` activa | PI |
| `FIN_CICLO` (v1.1.0) | 30 % (mínimo) cuando `G_est > 150 W/m²`; se apaga cuando `G_est < 100 W/m²` durante 60 s |
| `MODO_SEGURO` | 100 % |

En la versión 1.0.0 (lote L-01) el ventilador se apagaba en `FIN_CICLO`. Con sol, el colector quedó sin flujo y la temperatura a su salida llegó a 122 °C, cerca del límite del DS18B20 y por encima de lo conveniente para el film de la cubierta; además, el producto siguió secándose hasta quedar bajo 15 %. La versión 1.1.0 mantiene el ventilador al mínimo (30 %) mientras haya sol, y la humedad final objetivo pasó a 0,170.

### 8.4 Tacómetro

Se cuentan pulsos con interrupción en flanco de bajada durante cada ciclo de 60 s. Si `duty ≥ 30 %` y el ventilador marca menos de 400 rpm por tres ciclos (3 min), se emite el evento `falla_ventilador`. No cambia el estado; queda registrado y se avisa en el tablero.

---

## 9. Máquina de estados

### 9.1 Transiciones

| Desde | Evento | Hacia | Acciones |
|---|---|---|---|
| `ESPERA` | Pulsación corta con lectura estable | `CARGA` | Guarda `tara_cuentas`; crea el id de lote con el siguiente número (`L-xx`); evento `inicio_lote`; LED parpadeo lento |
| `CARGA` | Pulsación larga (> 3 s) con lectura estable y masa > 500 g | `OPERACION` | Guarda `m0_g`; `seq = 0`; abre el archivo del lote en la microSD; evento `masa_inicial` |
| `CARGA` | Sin acción por 30 min (cualquier pulsación reinicia el plazo) | `ESPERA` | Evento `carga_cancelada` |
| `OPERACION` | Masa filtrada < `m_obj` en 2 registros seguidos (20 min con el intervalo por defecto) | `FIN_CICLO` | Evento crítico `fin_ciclo`; LED parpadeo rápido |
| `OPERACION` | Dos pulsaciones largas en menos de 10 s | `ESPERA` | Cierre anticipado; evento `cierre_lote` con motivo `manual` |
| `FIN_CICLO` | Pulsación larga | `ESPERA` | Evento `cierre_lote`; cierra el archivo; borra el lote activo de NVS |
| `OPERACION` o `FIN_CICLO` | SHT31 de cámara inválido en 3 lecturas seguidas (3 min) | `MODO_SEGURO` | Guarda el estado previo; ventilador al 100 %; evento crítico `modo_seguro` |
| `MODO_SEGURO` | 6 lecturas válidas seguidas (6 min) | Estado previo | Evento `fin_modo_seguro` |

El flag `pausa` no es un estado: solo existe dentro de `OPERACION` y se envía en cada registro. En `MODO_SEGURO` el botón no actúa.

### 9.2 Fin de ciclo

```
m_ss  = m0 · (1 − x0)
m_obj = m_ss / (1 − xf)
```

- Al iniciar el lote, `x0` = 0,83 (provisional, configurable) y `x0_lab` = false. Con el valor provisional la detección queda inhibida durante las primeras 12 h; sirve de respaldo si se pierde el servidor. Al llegar la humedad gravimétrica (unas 7 h) se recalcula `m_obj` y `x0_lab` = true.
- Con una bandeja, la masa de la bandeja piloto es la del lote: `m_lote(t) = m(t)`.
- `xf` por lote: 0,165 en L-01; 0,170 en L-02 y L-03, para compensar la pérdida de masa entre la alerta y el retiro.

### 9.3 Botón y LED

| Acción | Duración |
|---|---|
| Corta | 50 ms a 1 s |
| Larga | > 3 s (se registra al cumplirse los 3 s, sin esperar a soltar; el LED se enciende fijo) |

| LED | Significado |
|---|---|
| Apagado | `ESPERA` |
| Parpadeo lento (1 Hz) | `CARGA` |
| Destello cada 5 s | `OPERACION` |
| Doble destello cada 5 s | `OPERACION` en pausa |
| Parpadeo rápido (4 Hz) | `FIN_CICLO` |
| Fijo | `MODO_SEGURO` |

---

## 10. Registro de datos

### 10.1 Documento JSON (≈320 bytes)

```json
{
  "eq": "SEC-01", "fw": "1.1.0", "lote": "L-02", "seq": 512,
  "ts": "2026-10-01T21:07:00Z",
  "masa_g": 2873.4, "t_cam": 54.62, "hr_cam": 23.1,
  "t_ent": 31.18, "hr_ent": 47.0, "t_col": 63.94,
  "luz_lux": 41260, "duty": 30.0,
  "estado": "operacion", "pausa": false,
  "n_descartes": 0, "senal_dbm": -61
}
```

Unidades: g, °C, % HR, lx, % de ciclo de trabajo, dBm. `ts` siempre en UTC. Los valores inválidos se envían como `null` (también `senal_dbm` sin enlace).

Solo se registra con un lote activo (`OPERACION`, `FIN_CICLO` o `MODO_SEGURO`). En `ESPERA` y `CARGA` no hay telemetría: el equipo publica solo `estado` y los eventos.

### 10.2 microSD

- Formato FAT32. Un archivo por lote: `/lotes/<lote>.jsonl`, un registro por línea.
- Archivo `/lotes/<lote>.ptr` con dos enteros: desplazamiento en bytes del primer registro no confirmado y su `seq`.
- Cada registro se escribe (abrir, agregar, `flush()`, cerrar) antes de enviarlo. El SD manda: `tComunicacion` siempre lee de la microSD, tanto lo nuevo como lo pendiente. Un mutex protege la microSD entre `tRegistro` y `tComunicacion`.
- Con 10 min de intervalo, un lote de 52 h ocupa alrededor de 100 kB.
- Si la microSD falla al escribir, se emite `error_sd`, la línea va a `qRegistros` y se transmite directo. Se reintenta montar la tarjeta en cada registro.

### 10.3 Reenvío de pendientes

1. Al reconectar el MQTT, `tComunicacion` lee el `.ptr` y recorre el archivo desde ese desplazamiento.
2. Publica hasta 10 mensajes sin confirmar (ventana) a un máximo de 5 por segundo.
3. Con cada PUBACK avanza el puntero si el mensaje confirmado es el más antiguo de la ventana.
4. Si la conexión cae a mitad, se repite desde el puntero; los duplicados los elimina Node-RED.
5. Si el lote se cierra con pendientes, se termina de enviar ese archivo antes de pasar al siguiente lote. Tras un reinicio solo se reenvían los pendientes del lote activo; los de un lote ya cerrado quedan en la microSD.

---

## 11. Comunicación

### 11.1 Enlace

```cpp
class Enlace {
 public:
  virtual bool iniciar() = 0;
  virtual bool conectado() = 0;
  virtual int8_t senal_dbm() = 0;
  virtual void mantener() = 0;   // reconexión con espera exponencial: 5, 10, 20, 40, 60 s
};
class EnlaceWiFi : public Enlace { /* WiFi.begin(), modo estación, ahorro de energía modem-sleep */ };
// class EnlaceCelular : public Enlace { /* AT por UART2; mismo contrato */ };
```

### 11.2 MQTT

| Parámetro | Valor |
|---|---|
| Broker | HiveMQ Cloud (plan gratuito), puerto 8883, TLS 1.2 |
| Verificación del servidor | Certificado raíz ISRG Root X1 embebido |
| Cliente | `SEC-01` (único); usuario y clave propios del equipo |
| Versión del protocolo | MQTT 3.1.1 (valor por defecto de esp-mqtt) |
| *Keep alive* | 60 s |
| Sesión | `clean_session = false` para conservar la suscripción a `config` |
| Última voluntad | `secador/SEC-01/estado` = `{"en_linea":false}`, QoS 1, retenido |

Tópicos (con identificador del equipo para admitir varios secadores):

| Tópico | Sentido | QoS | Retenido |
|---|---|---|---|
| `secador/SEC-01/telemetria` | equipo → servidor | 1 | No |
| `secador/SEC-01/evento` | equipo → servidor | 1 | No |
| `secador/SEC-01/estado` | equipo → servidor | 1 | Sí |
| `secador/SEC-01/config` | servidor → equipo | 1 | Sí |

Contenido de `estado` (al conectar y en cada cambio): `{"en_linea":true,"fw":"1.1.0","estado":"operacion","pausa":false,"lote":"L-02"}` (`lote` es `null` sin lote).

### 11.3 Mensaje de configuración

```json
{ "lote": "L-02", "x0": 0.8281, "x0_lab": true, "xf": 0.170, "setpoint": 60.0, "intervalo_s": 600 }
```

- `x0_lab` es opcional. Solo cuando viene en `true` el equipo marca `x0_lab = true` y quita la inhibición de 12 h; si falta, el `x0` se toma como provisional. El servidor lo envía en `true` al cargar la gravimetría y en todo mensaje posterior del lote.
- Los campos que faltan conservan su valor. Un mensaje igual a lo que ya hay (por ejemplo, el retenido al reconectar) no genera evento.
- Se acepta desde `CARGA`, porque el lote ya tiene id.

Validación en el equipo: `0,70 ≤ x0 ≤ 0,90`; `0,10 ≤ xf ≤ 0,25`; `45 ≤ setpoint ≤ 65`; `60 ≤ intervalo_s ≤ 1800` (600 por defecto). Un valor fuera de rango se rechaza entero y se responde con un evento `config_rechazada`. Si `lote` no coincide con el lote activo, se ignora. Lo aceptado se guarda en NVS y se confirma con el evento `config_aplicada`.

### 11.4 Hora

- El DS3231 da la hora al encender.
- Con red, se sincroniza por NTP (`pool.ntp.org`) cada 6 h y se corrige el DS3231 si la diferencia supera 2 s.
- Si el DS3231 reporta pérdida de oscilador y no hay red, el registro sale con `"ts": null` y el servidor le asigna la hora de recepción, marcada con `ts_servidor: true`.

---

## 12. Persistencia (NVS, espacio `secador`)

| Clave | Tipo | Contenido |
|---|---|---|
| `cal_S` | float | Factor de escala de la celda |
| `cal_CT` | float | Coeficiente térmico de la celda |
| `cal_K` | float | Factor lux por W/m² |
| `lote_id` | string | Lote activo |
| `lote_est` | uint8 | Estado del lote activo |
| `tara` | float | Tara en cuentas |
| `m0` | float | Masa inicial |
| `x0`, `xf`, `sp`, `int` | float / uint16 | Parámetros del lote |
| `x0_lab` | bool | `x0` viene de laboratorio |
| `t_ini` | uint32 | Época UTC de `masa_inicial` |
| `seq` | uint32 | Último `seq` (se escribe cada 10 registros y en cada evento, para no desgastar la flash) |
| `n_lote` | uint16 | Contador para generar el siguiente id (se fija con `lote n`) |

`MODO_SEGURO` no se guarda en `lote_est`: tras un reinicio se retoma el estado previo y, si el SHT31 sigue fallando, se vuelve a modo seguro en 3 min. Un lote en `CARGA` no se retoma.

Al arrancar: si `lote_id` existe y su estado es `OPERACION` o `FIN_CICLO`, el equipo retoma el lote. Toma `seq` del último registro del archivo `.jsonl`, que es más reciente que el de NVS, y emite el evento `reanudacion` con el motivo del reinicio (`esp_reset_reason()`).

---

## 13. Eventos

```json
{ "eq":"SEC-01", "lote":"L-02", "ts":"...", "tipo":"fin_ciclo",
  "nivel":"critico", "datos": { "masa_g": 1034.1, "m_obj_g": 1035.0 } }
```

`nivel` es `critico` para `fin_ciclo` y `modo_seguro`, e `info` para el resto. Los eventos esperan en una cola de 32 en RAM hasta que haya conexión; no se guardan en la microSD.

| Tipo | `datos` |
|---|---|
| `inicio_lote` | `tara_cuentas` |
| `masa_inicial` | `m0_g`, `m_obj_g`, `x0`, `xf` |
| `cierre_lote` | `motivo` (`manual` o `fin_ciclo`), `seq`, `masa_g` |
| `fin_ciclo` | `masa_g`, `m_obj_g` |
| `modo_seguro` | `previo` |
| `error_sensor` | `sensor` (`sht_cam`, `sht_ent`, `ds18b20`, `bh1750`, `hx711`) |
| `falla_ventilador` | `rpm`, `duty` |
| `config_aplicada` | parámetros vigentes y `m_obj_g` |
| `reanudacion` | `motivo` (`encendido`, `watchdog`, `panico`, `baja_tension`, …), `estado` |

Tipos: `inicio_lote`, `masa_inicial`, `carga_cancelada`, `cierre_lote`, `fin_ciclo` (crítico), `modo_seguro` (crítico), `fin_modo_seguro`, `pausa_inicio`, `pausa_fin`, `error_sensor`, `falla_ventilador`, `error_sd`, `config_aplicada`, `config_rechazada`, `reanudacion`.

---

## 14. Consola serie y pruebas de falla

Comandos siempre disponibles (son parte de la puesta en marcha):

| Comando | Efecto |
|---|---|
| `estado` | Imprime estado, lote, `m_obj`, parámetros, última lectura y calibración |
| `cal tara` / `cal S <masa_g>` | Calibración de la celda en dos puntos |
| `cal CT <valor>` / `cal K <valor>` | Carga de coeficientes |
| `lote n <num>` | Fija el contador de lotes (solo en `ESPERA`); el siguiente será `L-<num+1>` |

Comandos solo con `-DMODO_PRUEBAS`:

| Comando | Efecto |
|---|---|
| `prueba wdt` | Bloquea `tControl` para disparar el watchdog |
| `prueba sht_cam off` / `on` | Simula la desconexión del SHT31 de la cámara / la revierte |
| `prueba red off <min>` | Desactiva el enlace durante N minutos |

La desconexión real de sensores y los cortes de energía se hacen físicamente; los comandos sirven para repetir la prueba en mesa.

---

## 15. Servidor

### 15.1 Despliegue

- Máquina virtual Linux en la nube (1 vCPU, 1 GB de RAM) con Docker Compose (`servidor/docker-compose.yml`):
  - `nodered`: imagen `nodered/node-red:5.0.9` más el driver `mongodb` (`servidor/nodered/Dockerfile`). Solo accesible por el proxy.
  - `grafana`: imagen `grafana/grafana:13.3.0` (edición OSS; la imagen `grafana-oss` dejó de publicarse en la 13.0) con el complemento Infinity.
  - `caddy` como proxy inverso con HTTPS automático: `/api/*` y `/editor` van a Node-RED; el resto, a Grafana.
- MongoDB Atlas, clúster M0 gratuito, base `secador`.
- HiveMQ Cloud, plan gratuito. El servidor usa un usuario propio, distinto del de `SEC-01`.
- Todo lo variable (URL, claves, dominio, bot de Telegram) va en `servidor/.env`, a partir de `.env.ejemplo`. El paso a paso está en `servidor/MANUAL_DESPLIEGUE.md`.

### 15.2 Colecciones de MongoDB

Node-RED las crea al arrancar si no existen, junto con sus índices y la calibración inicial.

| Colección | Tipo | Campos clave | Índices |
|---|---|---|---|
| `mediciones` | Series de tiempo (`timeField: ts`, `metaField: meta {eq, lote}`, `granularity: minutes`) | Todos los del registro + `g_est`, `m_dot`, `eta`, `recibido` y `ts_servidor` | Único lógico por `meta.eq` + `meta.lote` + `seq` (se aplica en Node-RED; las series de tiempo no admiten índices únicos) |
| `eventos` | Normal | `eq`, `lote`, `ts`, `tipo`, `nivel`, `datos`, `recibido` | `{lote:1, ts:1}` |
| `lotes` | Normal | `_id` (lote), `eq`, `inicio`, `inicio_operacion`, `cierre`, `m0`, `x0`, `x0_lab`, `xf`, `m_obj`, `setpoint`, `intervalo_s`, `estado`, `gravimetria_inicial`, `gravimetria_final` | Ninguno adicional |
| `calibracion` | Normal | Documento `_id: "actual"` con `S`, `C_T`, `K`, `curva_caudal` (`[{duty, kg_s}]`) y `provisional` | Ninguno adicional |
| `rechazados` | Normal | `recibido`, `motivo`, `payload` | `{recibido:1}` |

La curva de caudal inicial es **de ejemplo** y está marcada `provisional: true`. Mientras no se cargue la curva medida, la η que se calcula no vale para el informe. Para cambiarla basta editar el documento en Atlas; Node-RED la vuelve a leer cada minuto.

### 15.3 Flujos de Node-RED

La lógica está en `servidor/nodered/lib/secador.js`, que se carga en `functionGlobalContext`. Los nodos `function` solo la llaman. Así se prueba con `node --test` sin levantar Node-RED (§16.3).

**Ingreso** (`mqtt in secador/+/telemetria` → `function: ingresar telemetría`):

- `validar`: comprueba campos obligatorios y tipos, rangos físicos (los mismos del firmware) y que `lote` exista en `lotes`. Lo que no pasa va a la colección `rechazados` con el motivo.
- `deduplicar`: mantiene en memoria un conjunto con la clave `eq|lote|seq|ts` de las últimas 24 h. Durante las primeras 24 h después de arrancar consulta además MongoDB (`eq` + `lote` + `seq`), porque el conjunto se reinició. Un duplicado se cuenta y se descarta.
- Telemetría y eventos se procesan **de a uno y en orden de llegada**. Llegan por tópicos distintos, y en paralelo la primera telemetría de un lote podía validarse antes de que `masa_inicial` lo creara.
- El cliente MQTT del servidor usa sesión persistente (`clean_session = false`) con QoS 1. Si Node-RED se reinicia, el broker guarda lo que llegue mientras tanto (el equipo ya recibió el PUBACK y no lo reenvía).

**Cálculo**:

```
m_ss  = m0 · (1 − x0)
X_bh  = 1 − m_ss / masa_g
X_bs  = (masa_g − m_ss) / m_ss
MR    = X_bs / X_bs0,   X_bs0 = x0 / (1 − x0)
G_est = luz_lux / K
ṁ     = interpolación lineal en la curva de caudal (duty → kg/s), saturada en los extremos
η     = ṁ · 1006 · (t_col − t_ent) / (0,65 · G_est)    solo si G_est > 200 W/m² y duty > 0
avance = (m0 − masa_g) / (m0 − m_obj)
```

- `G_est`, `ṁ` y `η` no dependen de la humedad de laboratorio y se guardan al insertar.
- `X_bh`, `X_bs`, `MR` y `avance` dependen de `x0`, así que se calculan **al leer** (API y CSV) con los datos vigentes del lote. Valen `null` mientras `x0_lab` sea `false`. Cuando llega la gravimetría quedan disponibles para todos los registros, incluidos los anteriores, sin recalcular nada en la base.

**Eventos** (`mqtt in secador/+/evento` → `function: ingresar evento`). Se guarda cada evento y se actualiza el lote:

| Evento | Efecto en `lotes` |
|---|---|
| `inicio_lote` | Crea el lote: `eq`, `inicio`, `estado: carga`, `tara_cuentas` |
| `masa_inicial` | `m0`, `x0` (provisional), `xf`, `m_obj`, `inicio_operacion`, `estado: operacion` |
| `carga_cancelada` | `estado: cancelado` |
| `fin_ciclo` | `estado: fin_ciclo` |
| `modo_seguro` / `fin_modo_seguro` | `estado: modo_seguro` / vuelve al estado previo |
| `cierre_lote` | `estado: cerrado`, `cierre`, `motivo_cierre` |
| `config_aplicada` | Parámetros que el equipo confirmó |
| `reanudacion` | `estado` informado por el equipo |

`sin_datos`: si no llega telemetría en 5 intervalos **con un lote activo** (`operacion`, `fin_ciclo` o `modo_seguro`), se guarda el evento `sin_datos` (crítico). Sin lote no hay telemetría, por diseño del firmware (§10.1).

**Configuración y gravimetría** (solo el usuario `tecnico`). Cada endpoint valida, guarda en `lotes` y publica el mensaje **retenido** en `secador/<eq>/config` con todos los parámetros vigentes, incluido `x0_lab` (§11.3):

| Endpoint | Cuerpo | Efecto |
|---|---|---|
| `POST /api/lotes/:id/config` | `{x0?, xf?, setpoint?, intervalo_s?}` | Cambia parámetros; un `x0` escrito aquí es provisional (`x0_lab: false`) |
| `POST /api/lotes/:id/gravimetria/inicial` | `{x0}` | Humedad de laboratorio: `x0_lab: true`, recalcula `m_obj` |
| `POST /api/lotes/:id/gravimetria/final` | `{xf_medida}` | Solo la guarda, para el informe |

**API para Grafana** (`http in` → `function: api` → `http response`). Usa autenticación básica con dos usuarios: `grafana` (solo lectura) y `tecnico` (lectura, configuración y gravimetría).

| Endpoint | Devuelve |
|---|---|
| `GET /api/lotes?eq=` | Lista de lotes |
| `GET /api/series?lote=&desde=&hasta=&campos=` | Serie de tiempo (JSON) con los derivados; `desde`/`hasta` en ISO o milisegundos |
| `GET /api/ultimo?eq=` | Una fila por equipo: último registro con derivados, datos del lote, `estado_panel`, `en_linea`, `min_sin_datos`, `alerta_fin_ciclo`, `alerta_modo_seguro` |
| `GET /api/eventos?lote=&eq=&limite=` | Eventos, del más reciente al más antiguo |
| `GET /api/rechazados?desde=&hasta=` | Registros rechazados y su motivo |
| `GET /api/export.csv?lote=` | Exportación CSV completa con derivados |

### 15.4 Grafana

- Origen de datos: complemento **Infinity** (gratuito, de Grafana Labs) apuntando a la API de Node-RED, con el usuario `grafana`. Se descartó el conector oficial de MongoDB por ser de la edición Enterprise; también un conector comunitario, para no depender de un complemento sin mantenimiento asegurado.
- Todo se aprovisiona con archivos (`servidor/grafana/provisioning`): origen de datos, tableros, alertas, plantilla de mensaje y punto de contacto. Los tableros se generan con `grafana/generar_tableros.py`.
- Usuarios, equipos y permisos se crean con `grafana/configurar-equipos.js`, porque Grafana no permite aprovisionarlos con archivos:
  - `operario` (rol *Viewer*): solo ve la carpeta *Operario*, con el tablero "Secador - Operario".
  - `tecnico` (rol *Editor*): ve las dos carpetas y edita *Técnico*.
- Tablero **"Secador - Operario"** (para el celular): estado en una palabra con color (`Secando`, `En pausa`, `FIN: retire la bandeja`, `Modo seguro`, `Sin conexión`); avance del secado (indicador de aguja); masa actual y objetivo; temperatura de la cámara y setpoint; horas de secado; lote; últimos 5 eventos.
- Tablero **"Secador - Técnico"**, con variables `eq`, `lote` y rango de tiempo: masa y `m_obj`; `X_bh` y `MR`; temperaturas (cámara, entrada, colector, setpoint); humedades relativas; `G_est` y duty; η; calidad de datos (`n_descartes`, `senal_dbm`); registros rechazados; eventos del lote; enlace a `export.csv`.
- Reglas de alerta (evaluación cada 1 min sobre `/api/ultimo`, una instancia por equipo):
  - `Fin de ciclo`: `alerta_fin_ciclo > 0` (el lote está en `fin_ciclo`).
  - `Modo seguro`: `alerta_modo_seguro > 0`.
  - `Sin datos`: `alerta_sin_datos > 0`: con un lote activo, más de dos intervalos de registro sin telemetría (20 min con el intervalo por defecto).
- Punto de contacto: Telegram (bot del proyecto) al celular del operario y al grupo del equipo, con un mensaje en español que indica la acción ("Fin de ciclo en SEC-01: retire la bandeja"). Repite cada hora mientras la alerta siga activa.
- Cada usuario entra directo a su tablero, y la interfaz está en español.

### 15.5 Página de operación (`/app/`)

Grafana sirve para mirar datos, pero no tiene formularios para cargarlos. Para el uso diario hay una página propia, servida por Node-RED (`servidor/nodered/publico/index.html`, sin dependencias, pensada para el celular). Usa la misma API:

- **Vista del operario** (usuario `operario`, solo lectura): el estado en una palabra y con color, junto con la instrucción ("FIN: retire la bandeja. Mantenga el botón presionado 3 s para cerrar el lote"); el avance con una barra; la masa y su objetivo; la temperatura y el setpoint; las horas de secado; dos gráficas del lote en curso (masa con la masa objetivo, temperatura de la cámara con el setpoint); los últimos eventos con nombres legibles; y los pasos para iniciar y cerrar un lote. Se actualiza cada 30 s.
- **Tablero del técnico** (usuario `tecnico`, pestaña propia), con selector de lote y de rango (lote completo, 24 h, 6 h, 1 h):
  - Indicadores: masa y masa perdida, avance, `X_bh`, `MR`, `ṁ` (último y promedio), η, `G_est`, duty, `T_cam` (media, mínima y máxima), `HR_cam`, horas de secado, % del tiempo en pausa, completitud de registros (recibidos / esperados según el intervalo), descartes del filtro, señal WiFi mínima y minutos sin datos.
  - Gráficas, una magnitud por eje: masa y `m_obj`; `X_bh`; `MR`; temperaturas (cámara, entrada, colector, con el setpoint); humedades relativas (cámara, entrada); `G_est`; iluminancia; duty; `ṁ`; η; señal WiFi; descartes. Pasando el dedo o el puntero se ven los valores de cada instante.
  - Todos los eventos del lote con nivel y datos, y los últimos mensajes rechazados.
- **Acciones del técnico** (usuario `tecnico`): resumen del lote, humedad inicial de laboratorio (en %), setpoint, intervalo y humedad final objetivo (`xf`, 10 a 25 %), humedad final medida y descarga del CSV.
- Las gráficas se dibujan en SVG dentro de la misma página, sin bibliotecas externas, con los datos de `GET /api/series`.
- Los lotes se inician y se cierran en el secador, con el botón (§9.1). La página no lo reemplaza: muestra los pasos.
- La página pide usuario y clave en su propio formulario y los envía como autenticación básica. Con el encabezado `X-Requested-With: secador-app`, la API responde 401 sin `WWW-Authenticate` para que el navegador no abra su propia ventana. `GET /api/yo` devuelve el rol.

---

## 16. Pruebas

### 16.1 Pruebas unitarias (`env:native`)

| Prueba | Caso |
|---|---|
| `test_filtros` | Mediana con valores atípicos; ventana con `NAN` |
| `test_pi` | Saturación en 30 % y 100 %; el integrador queda acotado entre 30 % y 100 %; transferencia sin salto |
| `test_fin_ciclo` | Tres registros bajo `m_obj`; un registro aislado no dispara; con `x0` provisional no dispara antes de 12 h |
| `test_registro` | JSON válido, campos `null`, tamaño < 400 bytes; eventos; validación del mensaje de configuración |
| `test_estados` | Transiciones de la tabla 9.1, pausa de §8.2, ventilador en `FIN_CICLO` y `MODO_SEGURO` |

### 16.2 Pruebas de integración

Cada requisito RF/RNF del capítulo 4 tiene su prueba en el apartado 5.3 del informe, con el procedimiento y el criterio de aceptación.

### 16.3 Pruebas del servidor (`npm test` en `servidor/`)

Con `node --test` y un MongoDB 8.0 real en memoria (`mongodb-memory-server`): validación, interpolación, derivados, CSV, creación de colecciones, deduplicación (también después de reiniciar), ciclo de vida del lote por eventos, gravimetría y configuración retenida, permisos de la API, alertas de `/api/ultimo` y orden de procesamiento con mensajes simultáneos.

De punta a punta, en local y sin cuentas: `npm run dev` levanta MongoDB, un broker MQTT, Node-RED y Grafana, y `npm run simular` publica un lote como si fuera el ESP32.

---

## 17. Pendientes para escribir el código

- Escribir `secretos.h` con las credenciales reales (el certificado raíz ya está en la plantilla).
- Generar el bot de Telegram y registrar el `chat_id` del operario y del grupo en `servidor/.env`.
- Reemplazar la curva de caudal de ejemplo por la medida y quitar `provisional` (§15.2).
- Adjuntar en el apéndice: código fuente del firmware por módulos, `flows.json` y los JSON de los tableros.

---

## 18. Cambios de la versión 1.1 al código

Decisiones tomadas al escribir el código. Las secciones anteriores ya las incluyen.

### 18.1 Firmware `1.1.0`

| # | Cambio | Motivo | Secciones |
|---|---|---|---|
| 1 | Plataforma fijada en `espressif32 @ 8.0.0` (núcleo Arduino 3.3.12) | Compilación reproducible; la plataforma oficial ya trae el núcleo 3.x | §2 |
| 2 | Campo opcional `x0_lab` en `secador/config` | Sin él, el equipo no distingue un `x0` de laboratorio de uno provisional | §11.3 |
| 3 | Solo se registra con lote activo; en `ESPERA` y `CARGA` se publica solo `estado` | El archivo y `seq` existen desde `OPERACION`; Node-RED rechaza registros sin lote | §10.1, §11.2 |
| 4 | Comando `lote n`; `estado` y `cal …` siempre disponibles | Una placa nueva empezaría en `L-01` y chocaría con lotes existentes; la calibración es parte de la instalación | §12, §14 |
| 5 | Prueba `test_estados`; `estados.cpp` sin dependencias del hardware | La máquina de estados es la lógica de más riesgo y se puede probar sin placa | §4, §16.1 |
| 6 | `rpm1, rpm2` → `rpm` | Hay un solo tacómetro (GPIO 35) | §5 |
| 7 | `Lote.t_inicio` y claves NVS `t_ini`, `x0_lab` | La inhibición de 12 h debe sobrevivir a un reinicio | §5, §12 |
| 8 | Tara y `m0` con el promedio de 20 muestras estables | Evita promediar muestras de antes de cargar la bandeja | §7.3 |
| 9 | `tComunicacion` siempre envía desde la microSD; `qRegistros` solo si falla | Un solo camino para lo nuevo y lo pendiente; el puntero avanza con cada PUBACK | §5, §10.2 |
| 10 | Eventos en una cola de RAM hasta que haya conexión | Que un `fin_ciclo` sin red no se pierda en los primeros minutos | §13 |
| 11 | `MODO_SEGURO` no se guarda en NVS | Al reiniciar se vuelve al estado previo, que es el que importa | §12 |
| 12 | `senal_dbm` = `null` sin enlace | 0 dBm se leería como señal perfecta | §10.1 |
| 13 | Pulsación larga registrada a los 3 s | Coincide con el LED fijo y hace natural la doble pulsación | §9.3 |
| 14 | Integral al volver de `MODO_SEGURO` = 100 % | Transferencia sin salto | §8.1 |
| 15 | Lectura de todos los sensores cada 60 s (antes 10 s); registro y envío cada 10 min por defecto (antes 1 min), configurable de 1 a 30 min; `Ts` del PI = 60 s; fin de ciclo con 2 registros seguidos (antes 3) | Pedido del proyecto: menos datos y menos consumo. Con 10 min por registro, 3 registros atrasarían el aviso 30 min | §6, §7, §8, §9.1, §10.2, §11.3 |

### 18.2 Servidor

| # | Cambio | Motivo | Secciones |
|---|---|---|---|
| 1 | El lote se crea y actualiza solo, a partir de los eventos del equipo | `validar` exige el lote, y el documento no decía quién lo crea | §15.3 |
| 2 | Endpoints de gravimetría inicial y final; se omite el formulario `ui` | La humedad de laboratorio necesita una vía de entrada; un endpoint cubre ambos usos | §15.3 |
| 3 | Curva de caudal de ejemplo marcada `provisional` | Aún no hay datos medidos | §15.2 |
| 4 | Derivados que dependen de `x0` calculados al leer | Sin recálculo en la base al llegar la gravimetría; los registros anteriores quedan al día | §15.3 |
| 5 | Ingreso de telemetría y eventos en orden, de a uno | En paralelo, la primera telemetría de un lote se rechazaba por "lote inexistente" | §15.3 |
| 6 | Sesión MQTT persistente en Node-RED | Con sesión limpia se perdía lo publicado mientras Node-RED estaba caído | §15.3 |
| 7 | `sin_datos` y la alerta "Sin datos" solo con lote activo | Sin lote el equipo no envía telemetría (§10.1) y la alerta sonaría toda la noche | §15.3, §15.4 |
| 8 | Dos usuarios de API (`grafana`, `tecnico`) | Grafana no debe poder cambiar la configuración del equipo | §15.3 |
| 9 | Colección `rechazados` en la tabla y endpoint `/api/rechazados` | Ya se usaba en el texto; el tablero técnico la muestra | §15.2, §15.3 |
| 10 | `/api/ultimo` devuelve una fila por equipo, con campos de alerta y `estado_panel` | Las alertas de Grafana necesitan valores numéricos por equipo | §15.3, §15.4 |
| 11 | Imagen `grafana/grafana:13.3.0` en lugar de `grafana-oss` | `grafana-oss` dejó de publicarse en la 13.0 | §15.1 |
| 12 | Lógica en `lib/secador.js`, colecciones creadas por Node-RED | Pruebas con `node --test`; no hace falta un script de inicialización aparte | §15.2, §15.3, §16.3 |
| 13 | Usuarios y permisos de Grafana con un script | Grafana no los aprovisiona con archivos | §15.4 |
| 14 | Página de operación `/app/` con usuario `operario`, y endpoint `/api/yo` | Grafana no permite cargar datos; el operario necesita una pantalla simple | §15.5 |
| 15 | El `eq` del mensaje debe coincidir con el del tópico y tener formato válido; `config_aplicada` y `reanudacion` se validan | Un mensaje no puede hacerse pasar por otro equipo ni escribir valores fuera de rango | §15.3 |
| 16 | El editor de Node-RED se desactiva si falta su clave | Que un `.env` incompleto no deje el editor abierto en internet | §15.1 |
| 17 | Un equipo no puede escribir telemetría ni eventos en el lote de otro; el `eq` del lote no se reasigna | Aislamiento entre secadores | §15.3 |
| 18 | `operario` y `tecnico` usan la misma clave en `/app/` y en Grafana | Una sola clave por persona | §15.4, §15.5 |
| 19 | Gráficas en `/app/` para el operario; pestaña "Tablero" del técnico con indicadores, gráficas, eventos y rechazados; `xf` editable desde la página | Ver los datos sin entrar a Grafana, cada rol con lo que necesita | §15.5 |
| 20 | Intervalo por defecto de 600 s, rango hasta 1800 s; alerta "Sin datos" y estado "Sin conexión" a los dos intervalos sin telemetría (`alerta_sin_datos` en `/api/ultimo`) | Acompaña el cambio 15 del firmware; con el umbral fijo de 5 min la alerta saltaba entre registros | §11.3, §15.3, §15.4 |
