// Serialización de registros y eventos, y lectura del mensaje de configuración (§10.1, §11.3, §13).
#pragma once
#include <stddef.h>
#include "tipos.h"

const char* nombre_estado(Estado e);
const char* nombre_evento(TipoEvento t);

// Escriben JSON en buf; devuelven la longitud o 0 si no cabe.
size_t registro_a_json(const Registro& r, const char* lote, char* buf, size_t n);
// datos: objeto JSON ya armado ("{...}") o nullptr. ts vacío o nullptr = null.
size_t evento_a_json(TipoEvento t, const char* lote, const char* ts, const char* datos, char* buf, size_t n);

enum class ResultadoConfig : uint8_t { APLICADA, RECHAZADA, IGNORADA, SIN_CAMBIOS };
// Valida y aplica sobre p solo si todo está en rango y el lote coincide.
ResultadoConfig config_desde_json(const char* json, size_t len, const char* lote_activo, ParametrosLote& p);
