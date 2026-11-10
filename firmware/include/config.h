// Constantes y valores por defecto (documentación §3, §7, §8, §9, §11).
#pragma once
#include <stdint.h>

#ifndef FW_VERSION
#define FW_VERSION "0.0.0"
#endif
#ifndef SECADOR_ID
#define SECADOR_ID "SEC-00"
#endif

// --- Pines (§3) ---
constexpr int PIN_SDA = 21, PIN_SCL = 22;
constexpr uint32_t I2C_HZ = 100000;
constexpr uint8_t SHT_ENT = 0x44, SHT_CAM = 0x45, BH1750_DIR = 0x23;
constexpr int PIN_HX_DOUT = 34, PIN_HX_SCK = 25;
constexpr int PIN_DS18B20 = 4;
constexpr int PIN_SD_CS = 5;
constexpr int PIN_PWM = 26, PWM_CANAL = 0, PWM_BITS = 10;
constexpr uint32_t PWM_HZ = 25000;
constexpr int PIN_VENT_ON = 27;
constexpr int PIN_TACO = 35;
constexpr int PIN_BOTON = 33;
constexpr int PIN_LED = 2;

// --- Calibración por defecto (si la NVS está vacía) ---
constexpr float CAL_S_DEF = 211.34f;   // cuentas/g
constexpr float CAL_CT_DEF = 0.465f;   // g/°C
constexpr float CAL_K_DEF = 46.9f;     // lx/(W/m²)

// --- Adquisición y filtros (§7) ---
constexpr uint32_t T_ADQ_S = 10;
constexpr int HX_MUESTRAS = 8;
constexpr float MASA_MIN_G = -200.0f, MASA_MAX_G = 10500.0f;
constexpr float DESCARTE_G = 15.0f;
constexpr float ESTABLE_DS_G = 2.0f;   // desviación estándar de las últimas 10 muestras
constexpr int ESTABLE_N = 10, TARA_N = 20;
constexpr int NAN_ERROR = 3;           // ciclos NAN seguidos para error_sensor
constexpr int MAX_LECT_INTERVALO = 32; // 300 s / 10 s = 30

// --- PI (§8.1) ---
constexpr float PI_KP = 7.0f, PI_KI = 0.0057f, PI_TS = 10.0f;
constexpr float DUTY_MIN = 30.0f, DUTY_MAX = 100.0f;
constexpr float SP_DEF = 60.0f;

// --- Pausa (§8.2) y fin de ciclo (§8.3) ---
constexpr float G_PAUSA_IN = 120.0f, DT_PAUSA_IN = 3.0f;  constexpr uint32_t T_PAUSA_IN_S = 600;
constexpr float G_PAUSA_OUT = 180.0f, DT_PAUSA_OUT = 6.0f; constexpr uint32_t T_PAUSA_OUT_S = 120;
constexpr float G_FIN_ON = 150.0f, G_FIN_OFF = 100.0f;    constexpr uint32_t T_FIN_OFF_S = 60;

// --- Tacómetro (§8.4) ---
constexpr int PULSOS_VUELTA = 2;
constexpr uint16_t RPM_MIN = 400;

// --- Máquina de estados (§9) ---
constexpr float MASA_MIN_CARGA_G = 500.0f;
constexpr uint32_t T_CARGA_MAX_S = 30 * 60;
constexpr uint32_t T_DOBLE_LARGA_S = 10;
constexpr int CAM_INVALIDAS_SEGURO = 3, CAM_VALIDAS_SALIDA = 6;
constexpr int REG_BAJO_FIN = 3;
constexpr uint32_t INHIBICION_PROV_S = 12 * 3600;
constexpr uint32_t BOTON_ANTIRREBOTE_MS = 50, BOTON_CORTA_MAX_MS = 1000, BOTON_LARGA_MS = 3000;

// --- Lote por defecto (§9.2, §11.3) ---
constexpr float X0_PROV = 0.83f, XF_DEF = 0.170f;
constexpr uint16_t INTERVALO_DEF_S = 60;

// --- Comunicación (§10.3, §11) ---
constexpr int VENTANA_MQTT = 10;
constexpr uint32_t PUB_ESPERA_MS = 200;    // 5 mensajes/s
constexpr uint32_t NTP_PERIODO_S = 6 * 3600;
constexpr int32_t NTP_TOLERANCIA_S = 2;
constexpr uint32_t WDT_MS = 30000;
