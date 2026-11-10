// microSD: /lotes/<lote>.jsonl y puntero de pendientes /lotes/<lote>.ptr (§10.2).
#pragma once
#include <stddef.h>
#include <stdint.h>

bool sd_iniciar();
bool sd_agregar(const char* lote, const char* linea);   // escribe la línea y hace flush
bool sd_leer_ptr(const char* lote, uint32_t& off, uint32_t& seq);
bool sd_escribir_ptr(const char* lote, uint32_t off, uint32_t seq);
// Lee la línea que empieza en `off`. Devuelve su longitud (0 = fin de archivo) y en off_sig el inicio de la siguiente.
size_t sd_leer_linea(const char* lote, uint32_t off, char* buf, size_t n, uint32_t& off_sig);
uint32_t sd_ultimo_seq(const char* lote);               // 0 si no hay archivo
uint32_t seq_de_linea(const char* linea);
