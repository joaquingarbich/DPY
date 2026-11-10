# Manual de carga y puesta en marcha del firmware del secador (v1.1.0)

Para quien nunca ha usado PlatformIO. Siga los pasos en orden. El diseño completo está en `../Documentacion_firmware_y_servidor_secador.md`. Las secciones que se citan aquí (§) se refieren a ese documento.

Necesita:
- Una computadora con Windows, macOS o Linux.
- El ESP32 DevKit V1 armado según la tabla de pines (§3).
- Un cable USB **de datos** (algunos cables solo cargan).
- Una masa patrón conocida (por ejemplo, 1000 g o 2000 g).

---

## 1. Instalar las herramientas (una sola vez)

1. Instale **Visual Studio Code**: https://code.visualstudio.com
2. Abra VS Code. Vaya a *Extensiones* (Ctrl+Shift+X), busque **PlatformIO IDE** e instálela. Espere a que termine; tarda varios minutos.
3. Reinicie VS Code.
4. Si usa Windows y la placa no aparece como puerto COM, instale el controlador del chip USB de la placa: **CP210x** (Silicon Labs) o **CH340**, según el chip que tenga.

## 2. Abrir el proyecto

1. *File → Open Folder…* y elija la carpeta `firmware` (la que tiene `platformio.ini`).
2. Espere a que PlatformIO termine de preparar el proyecto (barra inferior). La primera vez descarga la plataforma `espressif32 @ 8.0.0` y las bibliotecas, y puede tardar 10 minutos o más.

## 3. Credenciales (`secretos.h`)

1. En `include/`, copie `secretos.ejemplo.h` con el nombre **`secretos.h`**.
2. Edite `secretos.h`:
   - `WIFI_SSID`, `WIFI_CLAVE`: la red del secador.
   - `MQTT_HOST`: la URL del clúster de HiveMQ Cloud (*Overview → Cluster URL*), sin `mqtts://` ni el puerto.
   - `MQTT_USUARIO`, `MQTT_CLAVE`: las credenciales creadas en HiveMQ Cloud (*Access Management*) para este equipo.
   - El certificado `ROOT_CA` ya está incluido. No lo cambie.
3. `secretos.h` **no se sube a Git** (está en `.gitignore`). Guárdelo aparte de forma segura.

Si va a usar otro secador, cambie `-DSECADOR_ID=\"SEC-01\"` en `platformio.ini`.

## 4. Correr las pruebas (sin placa)

En la terminal de PlatformIO (ícono de la hormiga → *New Terminal*):

```
pio test -e native
```

Debe terminar con `27 test cases: 27 succeeded`. Si alguna falla, **no cargue el firmware** y avise.

> En Windows, `pio test -e native` necesita un compilador de C++ (MinGW-w64 / MSYS2) en el PATH. Si no lo tiene, corra las pruebas en macOS o Linux.

## 5. Compilar y cargar

1. Conecte la placa por USB.
2. Ejecute:

```
pio run -e esp32dev -t upload
pio device monitor
```

3. Al arrancar, el monitor debe mostrar `secador SEC-01 fw 1.1.0`.

Si sale `Failed to connect to ESP32`: mantenga presionado el botón **BOOT** de la placa cuando aparezca `Connecting....` y suéltelo cuando empiece a escribir.

Para cerrar el monitor: Ctrl+C. Para habilitar los comandos de prueba de fallas, quite el `;` de la línea `; -DMODO_PRUEBAS` en `platformio.ini` y vuelva a cargar. **No deje ese modo en el equipo de campo.**

## 6. Puesta en marcha por consola

Con `pio device monitor` abierto, escriba cada comando y pulse Enter.

### 6.1 Revisar los sensores

```
estado
```

Revise la línea de lecturas:
- `t_cam`, `t_ent`, `t_col` en °C razonables; `hr_cam` y `hr_ent` entre 0 y 100.
- `luz` mayor que 0 si hay luz.
- `flags 0x00`. Cualquier otro valor indica un sensor con error. Los bits valen: 0x01 SHT31 de cámara, 0x02 SHT31 de entrada, 0x04 DS18B20, 0x08 BH1750, 0x10 HX711, 0x20 BH1750 saturado.

Un `nan` en un valor indica un sensor desconectado o mal cableado. Revise el cableado con la tabla de §3: las resistencias de 4,7 kΩ en I2C y en 1-Wire, y la de 10 kΩ en el tacómetro (GPIO 35).

### 6.2 Calibrar la celda de carga

1. Deje la **bandeja vacía** y quieta unos 30 s.
2. Escriba `cal tara`. Responde `cero = … cuentas`. Si dice `lectura inestable`, espere y repita.
3. Ponga la **masa patrón** en el centro de la bandeja y espere 30 s.
4. Escriba `cal S <masa en g>`, por ejemplo `cal S 2000`. Responde `S = … cuentas/g guardado`. El valor de referencia es cercano a 211,34.
5. Retire la masa.

Si el informe trae otros coeficientes, cárguelos con estos comandos (si no, ya quedan los del documento):

```
cal CT 0.465
cal K 46.9
```

### 6.3 Fijar el contador de lotes

El equipo nombra los lotes `L-01`, `L-02`, … Si ya existen lotes en el servidor, indique el último número usado. Por ejemplo, si el último fue `L-03`:

```
lote n 3
```

Responde `el proximo lote sera L-04`. Solo funciona en `ESPERA`.

### 6.4 Comprobar todo

Escriba `estado` y verifique `cal S`, `CT`, `K` y `n_lote`. La calibración y el contador quedan en la memoria del equipo (NVS) y sobreviven a cortes de energía y a nuevas cargas del firmware.

> **Cuidado:** `pio run -t erase` borra la NVS: se pierden la calibración, el contador y el lote en curso. Úselo solo si sabe lo que hace, y repita la sección 6 después.

## 7. Operación con el botón y el LED

| Paso | Acción | LED |
|---|---|---|
| Equipo listo | — | Apagado (`ESPERA`) |
| 1. Tomar tara | Bandeja vacía y quieta ~30 s → **pulsación corta** | Parpadeo lento (`CARGA`) |
| 2. Confirmar carga | Cargar el mango, esperar ~30 s quieto → **pulsación larga** (> 3 s, hasta que el LED quede fijo) | Destello cada 5 s (`OPERACION`) |
| Pausa nocturna | Automática | Doble destello cada 5 s |
| Fin de ciclo | Llega la alerta por Telegram | Parpadeo rápido |
| 3. Cerrar lote | Retirar la bandeja → **pulsación larga** | Apagado |
| Cierre anticipado | **Dos pulsaciones largas** en menos de 10 s (en `OPERACION`) | Apagado |
| Modo seguro | Automático (falla el sensor de cámara) | Fijo; el ventilador al 100 % |

Si la pulsación no hace nada, la bandeja no está quieta o la masa es de 500 g o menos. Espere y repita.

## 8. Problemas frecuentes

| Síntoma | Causa probable | Qué hacer |
|---|---|---|
| `Failed to connect to ESP32` al cargar | La placa no entra en modo de carga | Mantener BOOT al conectar; probar otro cable o puerto USB |
| No aparece el puerto | Falta el controlador USB o el cable solo carga | Instalar CP210x/CH340; cambiar el cable |
| `[evento] … "error_sd"` | Falta la microSD, no está en FAT32 o está mal conectada | Formatear en FAT32; revisar CS = GPIO 5 |
| `[evento] … "error_sensor"` | Sensor desconectado | Revisar el cableado y las direcciones I2C (§3) |
| `[evento] … "falla_ventilador"` | Ventilador trabado o tacómetro sin pull-up | Revisar el ventilador y la resistencia de 10 kΩ en GPIO 35 |
| `senal -128 dBm` en `estado` / sin datos en Grafana | No hay WiFi | Revisar SSID y clave en `secretos.h`; acercar el router |
| Hay WiFi pero no llegan datos | Credenciales MQTT o host mal escritos | Revisar `MQTT_HOST`, usuario y clave; probar el usuario en el *Web Client* de HiveMQ |
| La masa da `nan` | HX711 desconectado, o masa fuera de −200 a 10 500 g | Revisar DOUT = 34, SCK = 25; repetir la calibración |
| `reanudacion` con motivo `watchdog` | Una tarea se bloqueó | Anotar la hora y avisar; el equipo retoma el lote solo |
| `reanudacion` con motivo `baja_tension` | Caída de tensión de la batería | Revisar la batería y la fuente |
