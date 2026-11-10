// LED de estado y consola serie (§9.3, §14).
#pragma once

void led_iniciar();
void led_tick();          // cada 10 ms
void consola_atender();   // desde loop()
