"""Genera los JSON de los tableros (dashboards/operario y dashboards/tecnico).
Editar aquí y volver a correr: python3 grafana/generar_tableros.py"""
import json, os

DS = {"type": "yesoreyeram-infinity-datasource", "uid": "secador-api"}
AQUI = os.path.dirname(os.path.abspath(__file__))

def consulta(url, columnas, formato="table", ref="A"):
    return {"refId": ref, "datasource": DS, "type": "json", "source": "url", "parser": "backend",
            "format": formato, "url": url, "url_options": {"method": "GET", "data": ""}, "root_selector": "",
            "columns": [{"selector": s, "text": t, "type": ty} for s, t, ty in columnas]}

def serie(campos):
    """Serie de tiempo del lote elegido: campos = [(campo, nombre)]"""
    url = "/api/series?lote=$lote&desde=${__from}&hasta=${__to}&campos=" + ",".join(c for c, _ in campos)
    return consulta(url, [("ts", "ts", "timestamp")] + [(c, n, "number") for c, n in campos], "timeseries")

def variable(nombre, etiqueta, url, col):
    return {"name": nombre, "label": etiqueta, "type": "query", "datasource": DS, "refresh": 1, "sort": 0,
            "query": {"queryType": "infinity", "query": "", "infinityQuery": consulta(url, [(col, col, "string")], ref="variable")},
            "definition": url}

def panel(id, titulo, tipo, x, y, w, h, targets, **extra):
    p = {"id": id, "title": titulo, "type": tipo, "datasource": DS, "gridPos": {"x": x, "y": y, "w": w, "h": h},
         "targets": targets if isinstance(targets, list) else [targets]}
    p.update(extra)
    return p

def tablero(uid, titulo, paneles, variables, refresco="1m", desde="now-24h"):
    return {"uid": uid, "title": titulo, "tags": ["secador"], "timezone": "browser", "schemaVersion": 39,
            "refresh": refresco, "time": {"from": desde, "to": "now"}, "editable": True,
            "templating": {"list": variables}, "panels": paneles}

def stat(unidad=None, decimales=None, mapeos=None, color="value", texto=None):
    d = {"color": {"mode": "thresholds"}, "thresholds": {"mode": "absolute", "steps": [{"color": "blue", "value": None}]}}
    if unidad: d["unit"] = unidad
    if decimales is not None: d["decimals"] = decimales
    if mapeos: d["mappings"] = mapeos
    return {"fieldConfig": {"defaults": d, "overrides": []},
            "options": {"reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False},
                        "colorMode": color, "graphMode": "none", "textMode": texto or "value_and_name", "justifyMode": "center",
                        "orientation": "horizontal"}}

ESTADOS = {
    "operacion": ("Secando", "green"), "pausa": ("En pausa (sin sol)", "blue"), "fin_ciclo": ("FIN: retire la bandeja", "orange"),
    "modo_seguro": ("Modo seguro: avise al técnico", "red"), "sin_conexion": ("Sin conexión", "red"),
    "carga": ("Cargando", "purple"), "cerrado": ("Lote cerrado", "text"), "cancelado": ("Carga cancelada", "text"),
    "sin_lote": ("Sin lote", "text"),
}
MAPEO_ESTADO = [{"type": "value", "options": {k: {"text": t, "color": c, "index": i} for i, (k, (t, c)) in enumerate(ESTADOS.items())}}]
ULTIMO = "/api/ultimo?eq=$eq"
VAR_EQ = variable("eq", "Equipo", "/api/ultimo", "eq")

# ---------------------------------------------------------------- operario
op = [
    panel(1, "Estado", "stat", 0, 0, 24, 5, consulta(ULTIMO, [("estado_panel", "Estado", "string")]),
          **stat(mapeos=MAPEO_ESTADO, color="background", texto="value")),
    panel(2, "Avance del secado", "gauge", 0, 5, 12, 8, consulta(ULTIMO, [("avance", "Avance", "number")]),
          fieldConfig={"defaults": {"unit": "percentunit", "min": 0, "max": 1, "decimals": 0,
                                    "thresholds": {"mode": "absolute", "steps": [{"color": "blue", "value": None}, {"color": "green", "value": 0.9}, {"color": "orange", "value": 1}]}},
                       "overrides": []},
          options={"reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False}, "showThresholdMarkers": True}),
    panel(3, "Masa", "stat", 12, 5, 12, 4, consulta(ULTIMO, [("masa_g", "Actual", "number"), ("m_obj", "Objetivo", "number")]),
          **stat("massg", 0)),
    panel(4, "Temperatura de la cámara", "stat", 12, 9, 12, 4, consulta(ULTIMO, [("t_cam", "Cámara", "number"), ("setpoint", "Setpoint", "number")]),
          **stat("celsius", 1)),
    panel(5, "Tiempo de secado", "stat", 0, 13, 12, 4, consulta(ULTIMO, [("horas_lote", "Horas", "number")]), **stat("h", 1, texto="value")),
    panel(6, "Lote", "stat", 12, 13, 12, 4, consulta(ULTIMO, [("lote", "Lote", "string")]), **stat(texto="value")),
    panel(7, "Últimos eventos", "table", 0, 17, 24, 8,
          consulta("/api/eventos?eq=$eq&limite=5", [("ts", "Hora", "timestamp"), ("tipo", "Evento", "string"), ("lote", "Lote", "string")]),
          options={"showHeader": True}),
]
operario = tablero("secador-operario", "Secador - Operario", op, [VAR_EQ], refresco="1m")

# ---------------------------------------------------------------- técnico
def ts(id, titulo, x, y, campos, unidad, w=12, h=8, **extra):
    fc = {"defaults": {"unit": unidad, "custom": {"lineWidth": 1, "spanNulls": False}}, "overrides": []}
    fc.update(extra.pop("fieldConfig", {}))
    return panel(id, titulo, "timeseries", x, y, w, h, serie(campos), fieldConfig=fc,
                 options={"legend": {"displayMode": "list", "placement": "bottom"}, "tooltip": {"mode": "multi"}}, **extra)

def eje_derecho(campo, unidad):
    return {"matcher": {"id": "byName", "options": campo},
            "properties": [{"id": "custom.axisPlacement", "value": "right"}, {"id": "unit", "value": unidad}]}

tec = [
    ts(1, "Masa", 0, 0, [("masa_g", "Masa"), ("m_obj", "Masa objetivo")], "massg"),
    ts(2, "Humedad del producto", 12, 0, [("x_bh", "X b.h."), ("mr", "MR")], "percentunit"),
    ts(3, "Temperaturas", 0, 8, [("t_cam", "Cámara"), ("t_ent", "Entrada"), ("t_col", "Colector"), ("setpoint", "Setpoint")], "celsius"),
    ts(4, "Humedad relativa", 12, 8, [("hr_cam", "Cámara"), ("hr_ent", "Entrada")], "humidity"),
    ts(5, "Radiación y ventilador", 0, 16, [("g_est", "G estimada (W/m²)"), ("duty", "Duty (%)")], "none",
       fieldConfig={"overrides": [eje_derecho("Duty (%)", "percent")]}),
    ts(6, "Eficiencia del colector", 12, 16, [("eta", "η")], "percentunit"),
    ts(7, "Calidad de datos", 0, 24, [("n_descartes", "Descartes"), ("senal_dbm", "Señal (dBm)")], "none",
       fieldConfig={"overrides": [eje_derecho("Señal (dBm)", "dBm")]}),
    panel(8, "Registros rechazados", "table", 12, 24, 12, 8,
          consulta("/api/rechazados?desde=${__from}&hasta=${__to}", [("recibido", "Recibido", "timestamp"), ("motivo", "Motivo", "string")])),
    panel(9, "Eventos del lote", "table", 0, 32, 24, 9,
          consulta("/api/eventos?lote=$lote", [("ts", "Hora", "timestamp"), ("tipo", "Evento", "string"), ("nivel", "Nivel", "string"), ("datos", "Datos", "string")])),
    {"id": 10, "title": "Exportar", "type": "text", "gridPos": {"x": 0, "y": 41, "w": 24, "h": 3},
     "options": {"mode": "markdown", "content": "[Descargar CSV del lote $lote](/api/export.csv?lote=$lote) · Requiere el usuario `tecnico` de la API.\n\nLa eficiencia usa la curva de caudal de la colección `calibracion`; mientras esté marcada como provisional, η no vale para el informe."}},
]
tecnico = tablero("secador-tecnico", "Secador - Técnico", tec,
                  [VAR_EQ, variable("lote", "Lote", "/api/lotes?eq=$eq", "id")], refresco="1m", desde="now-2d")

for carpeta, t in (("operario", operario), ("tecnico", tecnico)):
    with open(os.path.join(AQUI, "dashboards", carpeta, f"{t['uid']}.json"), "w", encoding="utf-8") as f:
        json.dump(t, f, ensure_ascii=False, indent=2)
print("ok")
