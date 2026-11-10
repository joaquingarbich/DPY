#include "almacenamiento.h"
#include "config.h"
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

// tRegistro escribe y tComunicacion lee: la biblioteca SD no es segura entre tareas.
static SemaphoreHandle_t mtx;
static bool ok = false;

struct Bloqueo {
  Bloqueo() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Bloqueo() { xSemaphoreGive(mtx); }
};

static String ruta(const char* lote, const char* ext) { return String("/lotes/") + lote + ext; }

bool sd_iniciar() {
  mtx = xSemaphoreCreateMutex();
  ok = SD.begin(PIN_SD_CS);
  if (ok && !SD.exists("/lotes")) ok = SD.mkdir("/lotes");
  return ok;
}

bool sd_agregar(const char* lote, const char* linea) {
  Bloqueo b;
  if (!ok && !(ok = SD.begin(PIN_SD_CS))) return false;  // reintenta si se reinsertó
  File f = SD.open(ruta(lote, ".jsonl"), FILE_APPEND);
  if (!f) { ok = false; return false; }
  size_t n = strlen(linea);
  bool bien = f.write((const uint8_t*)linea, n) == n && f.write('\n') == 1;
  f.flush();
  f.close();
  if (!bien) ok = false;
  return bien;
}

bool sd_leer_ptr(const char* lote, uint32_t& off, uint32_t& seq) {
  Bloqueo b;
  off = seq = 0;
  File f = SD.open(ruta(lote, ".ptr"), FILE_READ);
  if (!f) return false;
  off = f.parseInt();
  seq = f.parseInt();
  f.close();
  return true;
}

bool sd_escribir_ptr(const char* lote, uint32_t off, uint32_t seq) {
  Bloqueo b;
  File f = SD.open(ruta(lote, ".ptr"), FILE_WRITE);  // trunca
  if (!f) return false;
  f.printf("%lu %lu\n", (unsigned long)off, (unsigned long)seq);
  f.close();
  return true;
}

size_t sd_leer_linea(const char* lote, uint32_t off, char* buf, size_t n, uint32_t& off_sig) {
  Bloqueo b;
  off_sig = off;
  File f = SD.open(ruta(lote, ".jsonl"), FILE_READ);
  if (!f || !f.seek(off)) return 0;
  size_t k = f.readBytesUntil('\n', buf, n - 1);
  bool completa = f.position() > off + k;   // encontró el '\n'
  f.close();
  if (!completa) return 0;                  // línea a medio escribir: esperar
  buf[k] = '\0';
  off_sig = off + k + 1;
  return k;
}

uint32_t seq_de_linea(const char* linea) {
  const char* p = strstr(linea, "\"seq\":");
  return p ? strtoul(p + 6, nullptr, 10) : 0;
}

uint32_t sd_ultimo_seq(const char* lote) {
  Bloqueo b;
  File f = SD.open(ruta(lote, ".jsonl"), FILE_READ);
  if (!f) return 0;
  char buf[512];
  size_t tam = f.size();
  f.seek(tam > sizeof buf - 1 ? tam - (sizeof buf - 1) : 0);
  size_t k = f.read((uint8_t*)buf, sizeof buf - 1);
  f.close();
  buf[k] = '\0';
  const char* ult = nullptr;
  for (const char* p = buf; (p = strstr(p, "\"seq\":")); p++) ult = p;
  return ult ? seq_de_linea(ult) : 0;
}
