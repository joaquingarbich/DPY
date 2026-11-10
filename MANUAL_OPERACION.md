# Manual de operación del secador solar

Uso diario, para el **operario** y el **técnico**. La instalación está en `firmware/MANUAL.md` (equipo) y `servidor/MANUAL_DESPLIEGUE.md` (servidor).

| Qué | Dónde |
|---|---|
| Página de operación (celular) | `https://<dominio>/app/` |
| Gráficos y análisis | `https://<dominio>/` (Grafana) |
| Alertas | Telegram: el bot del proyecto escribe al operario y al grupo |

---

## 1. Operario

### 1.1 Iniciar un lote

Se hace **en el secador, con el botón**. Ni la página ni Grafana pueden iniciar un lote, porque el equipo tiene que pesar la bandeja.

| Paso | Qué hacer | Cómo saber que funcionó |
|---|---|---|
| 1. Tara | Bandeja **vacía**, sin tocarla unos 30 s. **Pulsación corta** (menos de 1 s) | El LED parpadea lento. En `/app/`: "Cargando la bandeja" |
| 2. Carga | Ponga el mango en la bandeja. Espere **30 s sin tocarla**. **Mantenga el botón 3 s**, hasta que el LED quede fijo, y suelte | El LED da un destello cada 5 s. En `/app/`: "Secando" (verde) |

Si la pulsación no hace nada, la bandeja se movió o la masa es de 500 g o menos. Espere quieto y repita. Si en la carga pasan 30 min sin confirmar, el equipo la cancela solo y vuelve al paso 1.

### 1.2 Durante el secado

Revise `/app/` de vez en cuando. El color indica qué hacer:

| Pantalla | Qué significa | Qué hacer |
|---|---|---|
| 🟢 **Secando** | Todo en orden | Nada |
| 🔵 **En pausa** | No hay sol; el ventilador está apagado (normal de noche) | Nada |
| 🟠 **FIN: retire la bandeja** | El mango llegó a la humedad final (también llega un Telegram) | Ir al paso 1.3 |
| 🔴 **Avise al técnico** | Falla el sensor de la cámara; el ventilador está al máximo para proteger el mango | Avisar al técnico |
| 🔴 **Sin conexión** | El secador no envía datos (también llega un Telegram) | Revisar que tenga energía y WiFi. El equipo sigue secando y guarda los datos; los envía al volver la red |

El **avance** (%) aparece cuando el técnico carga la humedad de laboratorio, unas 7 h después del inicio.

### 1.3 Terminar el lote

1. Retire la bandeja.
2. **Mantenga el botón 3 s.** El LED se apaga y en `/app/` aparece "Sin lote en curso".

**Cierre anticipado** (por ejemplo, si llueve): **dos pulsaciones largas** (3 s cada una) en menos de 10 s.

### 1.4 El LED del secador

| LED | Estado |
|---|---|
| Apagado | Esperando (sin lote) |
| Parpadeo lento | Cargando |
| Un destello cada 5 s | Secando |
| Dos destellos cada 5 s | En pausa (sin sol) |
| Parpadeo rápido | Fin de ciclo: retire la bandeja |
| Fijo | Modo seguro: avise al técnico (o botón mantenido más de 3 s) |

---

## 2. Técnico

Entre a `/app/` con el usuario `tecnico`. La pestaña **Acciones del técnico** tiene todo lo que se carga a mano.

### 2.1 Humedad inicial de laboratorio (cada lote)

1. Tome la muestra al iniciar el lote y haga la gravimetría.
2. En **Acciones del técnico**, elija el lote y escriba la humedad en base húmeda, en %, por ejemplo `82.81`. Pulse **Guardar y enviar**.
3. En la lista de eventos aparece **"Configuración aplicada"**: el secador la recibió.

**Por qué importa:** hasta que llega este dato, el equipo usa 83 % como valor provisional y no avisa el fin de ciclo antes de las 12 h, por seguridad. Con el dato real calcula bien la masa objetivo. Además, recién entonces aparecen en la página y en Grafana el avance y la humedad del producto.

### 2.2 Otras acciones

| Acción | Cuándo |
|---|---|
| **Setpoint de la cámara** (45 a 65 °C) | Para probar otra temperatura de secado |
| **Intervalo de registro** (1 a 5 min) | Normalmente 1 min |
| **Humedad final medida** | Al terminar, con la gravimetría del producto seco. Solo queda guardada para el informe |
| **Descargar CSV** | Para el informe: todos los registros del lote con los valores calculados |

Si el secador está sin red, la configuración le llega al reconectarse. Si aparece **"Configuración rechazada"**, algún valor estaba fuera de rango.

### 2.3 Grafana (análisis)

En `https://<dominio>/`, con el usuario `tecnico`, el tablero **Secador - Técnico** muestra para el lote elegido:
- Masa y masa objetivo.
- Humedad del producto y MR.
- Temperaturas, humedades relativas, radiación y ventilador.
- Eficiencia del colector.
- Calidad de los datos, registros rechazados y eventos.

Arriba se elige el lote y el rango de tiempo. Arrastrando sobre un gráfico se hace zoom.

### 2.4 Alertas (Telegram)

| Alerta | Cuándo llega |
|---|---|
| Fin de ciclo: retire la bandeja | El lote llegó a la masa objetivo |
| Modo seguro | Falla el sensor de la cámara |
| Sin datos | Pasan más de 5 min sin datos con un lote en curso |

Se repiten cada hora mientras sigan activas. Cuando se resuelven, llega "✅ Resuelto".

### 2.5 Calibración (al instalar o cambiar sensores)

Por la consola serie del equipo (`firmware/MANUAL.md`, sección 6):
- `cal tara` y `cal S <masa>` para la celda de carga.
- `cal CT` y `cal K` para los coeficientes.
- `lote n <número>` para el contador de lotes.

**Curva de caudal:** la que trae el servidor es de ejemplo. Cuando tengan la medida, cárguenla en MongoDB Atlas (`servidor/MANUAL_DESPLIEGUE.md`, parte 5). Mientras tanto, la eficiencia η no vale para el informe.

---

## 3. Problemas frecuentes

| Problema | Qué hacer |
|---|---|
| La pulsación no hace nada | Esperar 30 s con la bandeja quieta y repetir. En la carga, la masa debe superar 500 g |
| "Sin conexión" durante mucho tiempo | Revisar la energía y el router. Los datos quedan en la microSD y se envían solos al volver la red |
| No llegan alertas a Telegram | El operario debe haberle escrito al bot al menos una vez. El técnico puede probar en Grafana: *Alertas → Puntos de contacto → Probar* |
| No aparece el avance | Falta cargar la humedad inicial de laboratorio (2.1) |
| "Avise al técnico" | Revisar el cable y el sensor SHT31 de la cámara. El equipo vuelve solo a la normalidad cuando el sensor responde 1 min seguido |
| Olvidé la clave | Pedirla al responsable: está en `servidor/.env` |
