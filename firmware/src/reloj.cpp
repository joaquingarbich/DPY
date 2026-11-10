#include "reloj.h"
#include "config.h"
#include <Arduino.h>
#include <RTClib.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

static RTC_DS3231 rtc;
static bool hay_rtc = false;
static volatile bool valido = false, ntp_nuevo = false;
static bool ntp_iniciado = false;

static void al_sincronizar(struct timeval*) { valido = true; ntp_nuevo = true; }

void reloj_iniciar() {
  setenv("TZ", "UTC0", 1);
  tzset();
  hay_rtc = rtc.begin();
  if (hay_rtc && !rtc.lostPower()) {
    timeval tv = {(time_t)rtc.now().unixtime(), 0};
    settimeofday(&tv, nullptr);
    valido = true;
  }
}

bool reloj_valido() { return valido; }

uint32_t reloj_epoch() { return valido ? (uint32_t)time(nullptr) : 0; }

void reloj_ts(char out[21]) {
  out[0] = '\0';
  if (!valido) return;
  time_t t = time(nullptr);
  tm u;
  gmtime_r(&t, &u);
  strftime(out, 21, "%Y-%m-%dT%H:%M:%SZ", &u);
}

void reloj_iniciar_ntp() {
  if (ntp_iniciado) return;
  ntp_iniciado = true;
  sntp_set_sync_interval(NTP_PERIODO_S * 1000UL);
  sntp_set_time_sync_notification_cb(al_sincronizar);
  configTime(0, 0, "pool.ntp.org");
}

void reloj_mantener() {
  if (!ntp_nuevo || !hay_rtc) return;
  ntp_nuevo = false;
  uint32_t ahora = (uint32_t)time(nullptr);
  int32_t dif = (int32_t)(rtc.now().unixtime() - ahora);
  if (rtc.lostPower() || dif > NTP_TOLERANCIA_S || dif < -NTP_TOLERANCIA_S)
    rtc.adjust(DateTime(ahora));
}
