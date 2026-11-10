// Hora: DS3231 al encender, NTP cada 6 h (§11.4).
#pragma once
#include <stdint.h>

void reloj_iniciar();
bool reloj_valido();
uint32_t reloj_epoch();          // 0 si la hora no es válida
void reloj_ts(char out[21]);     // "AAAA-MM-DDTHH:MM:SSZ" o "" si no es válida
void reloj_iniciar_ntp();        // al tener red por primera vez
void reloj_mantener();           // corrige el DS3231 tras cada sincronización NTP
