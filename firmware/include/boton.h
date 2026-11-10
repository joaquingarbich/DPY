// Botón de lote: pulsación corta / larga con antirrebote (§9.3).
#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

void boton_iniciar(QueueHandle_t eventos);   // revisa el pin cada 10 ms
void boton_tick();                           // lo llama el temporizador de 10 ms
bool boton_larga_en_curso();                 // LED fijo mientras se mantiene > 3 s
