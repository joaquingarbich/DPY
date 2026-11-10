#include "sensores.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BH1750.h>
#include <HX711.h>

static Adafruit_SHT31 sht_ent, sht_cam;
static OneWire bus(PIN_DS18B20);
static DallasTemperature ds(&bus);
static BH1750 luz(BH1750_DIR);
static HX711 hx;
static bool sim_cam_off = false;

void sensores_iniciar() {
  Wire.begin(PIN_SDA, PIN_SCL, I2C_HZ);
  sht_ent.begin(SHT_ENT);
  sht_cam.begin(SHT_CAM);
  ds.begin();
  ds.setResolution(12);
  ds.setWaitForConversion(false);
  luz.begin(BH1750::ONE_TIME_HIGH_RES_MODE);
  hx.begin(PIN_HX_DOUT, PIN_HX_SCK);
}

void sensores_simular_sht_cam(bool d) { sim_cam_off = d; }

static void leer_sht(Adafruit_SHT31& s, float& t, float& h, uint8_t& flags, uint8_t bit) {
  bool ok = s.readBoth(&t, &h);  // alta repetibilidad, sin clock stretching, con CRC
  if (!ok || isnan(t) || isnan(h) || t < -10 || t > 90 || h < 0 || h > 100) {
    t = h = NAN;
    flags |= bit;
  }
}

void sensores_leer(Lectura& l, float cuentas[HX_MUESTRAS]) {
  l.flags = 0;
  l.t_ms = millis();
  ds.requestTemperatures();            // conversión asíncrona; se lee ≥800 ms después
  uint32_t t_ds = millis();

  leer_sht(sht_cam, l.t_cam, l.hr_cam, l.flags, F_SHT_CAM);
  if (sim_cam_off) { l.t_cam = l.hr_cam = NAN; l.flags |= F_SHT_CAM; }
  leer_sht(sht_ent, l.t_ent, l.hr_ent, l.flags, F_SHT_ENT);

  luz.configure(BH1750::ONE_TIME_HIGH_RES_MODE);
  uint32_t t_bh = millis();
  while (!luz.measurementReady(true) && millis() - t_bh < 300) delay(10);
  float lux = luz.readLightLevel();
  if (lux < 0) { l.luz_lux = NAN; l.flags |= F_BH1750; }
  else {
    l.luz_lux = lux;
    if (lux >= 65535 / 1.2f - 0.5f) l.flags |= F_LUZ_SAT;  // cuenta bruta 65 535 (MTreg 69)
  }

  // HX711: 8 lecturas a 10 SPS (≈0,8 s)
  for (int i = 0; i < HX_MUESTRAS; i++) {
    cuentas[i] = NAN;
    if (!hx.wait_ready_timeout(200)) continue;
    long c = hx.read();
    if (c > -8388608 && c < 8388607) cuentas[i] = (float)c;  // extremos = saturado
  }

  while (millis() - t_ds < 800) delay(10);
  float t = ds.getTempCByIndex(0);
  if (t == DEVICE_DISCONNECTED_C || t == 85.0f || t < -10 || t > 125) { l.t_col = NAN; l.flags |= F_DS18B20; }
  else l.t_col = t;
}
