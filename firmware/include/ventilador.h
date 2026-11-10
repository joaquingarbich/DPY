// PWM a 25 kHz, interruptor de alimentación y tacómetro (§3, §8.4).
#pragma once
#include <stdint.h>

void ventilador_iniciar();
void ventilador_aplicar(bool encendido, float duty);
uint16_t ventilador_rpm();   // rpm desde la llamada anterior
