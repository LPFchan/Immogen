#include "immo_provisioning.h"
#include "immo_crypto.h"
#include <Arduino.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

namespace immo {
namespace {

bool hex_byte(const char* hex, uint8_t* out) {
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  int hi = nib(hex[0]);
  int lo = nib(hex[1]);
  if (hi < 0 || lo < 0) return false;
  *out = static_cast<uint8_t>((hi << 4) | lo);
  return true;
}

bool parse_counter_hex(const char* hex, uint32_t* out) {
  uint32_t val = 0;
  for (int i = 0; i < 8; i++) {
    int nib = -1;
    if (hex[i] >= '0' && hex[i] <= '9') nib = hex[i] - '0';
    else if (hex[i] >= 'A' && hex[i] <= 'F') nib = hex[i] - 'A' + 10;
    else if (hex[i] >= 'a' && hex[i] <= 'f') nib = hex[i] - 'a' + 10;
    if (nib < 0) return false;
    val = (val << 4) | (uint32_t)nib;
  }
  *out = val;
  return true;
}

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int k = 0; k < 8; k++)
      crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
  }
  return crc;
}

}  // namespace

bool prov_is_vbus_present() {
#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA)
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
#else
  return false;
#endif
}

bool prov_run_serial_loop(uint32_t timeout_ms, bool (*on_success)(const uint8_t[16], uint32_t)) {
  const uint32_t deadline = millis() + timeout_ms;
  char line[128];
  size_t len = 0;

  while (len < sizeof(line) - 1) {
    // Enforce deadline only before data has started arriving; once PROV: is streaming, let it complete.
    if (len == 0 && millis() >= deadline) return false;

    if (!Serial.available()) {
      delay(10);
      continue;
    }
    int c = Serial.read();
    if (c < 0) continue;
    if (c == '\n' || c == '\r') {
      line[len] = '\0';
      if (len == 0) continue;

      if (strncmp(line, "PROV:", 5) != 0) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      const char* rest = line + 5;
      const char* col1 = strchr(rest, ':');
      const char* col2 = col1 ? strchr(col1 + 1, ':') : nullptr;
      if (!col1 || !col2) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      const char* key_hex = rest;
      const char* counter_hex = col1 + 1;
      const char* checksum_hex = col2 + 1;
      
      if ((size_t)(col1 - key_hex) != 32 || (size_t)(col2 - counter_hex) != 8 || strlen(checksum_hex) != 4) {
        Serial.println("ERR:MALFORMED");
        return false;
      }

      uint8_t key_buf[16];
      for (size_t i = 0; i < 16; i++) {
        if (!hex_byte(key_hex + i * 2, &key_buf[i])) {
          Serial.println("ERR:MALFORMED");
          return false;
        }
      }
      uint32_t counter_val;
      if (!parse_counter_hex(counter_hex, &counter_val)) {
        Serial.println("ERR:MALFORMED");
        return false;
      }

      uint8_t checksum_hi = 0, checksum_lo = 0;
      if (!hex_byte(checksum_hex, &checksum_hi) || !hex_byte(checksum_hex + 2, &checksum_lo)) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      uint16_t checksum_received = (uint16_t)checksum_hi << 8 | checksum_lo;
      if (crc16_ccitt(key_buf, 16) != checksum_received) {
        Serial.println("ERR:CHECKSUM");
        return false;
      }

      if (on_success && on_success(key_buf, counter_val)) {
        Serial.println("ACK:PROV_SUCCESS");
        return true;
      }
      if (on_success) Serial.println("ERR:STORAGE");
      return false;
    }
    line[len++] = (char)c;
  }
  return false;
}

void ensure_provisioned(
    uint32_t timeout_ms,
    bool (*on_success)(const uint8_t[16], uint32_t),
    void (*load_provisioning)(),
    bool (*is_provisioned)()
) {
  if (prov_is_vbus_present()) {
    prov_run_serial_loop(timeout_ms, on_success);
    if (load_provisioning) load_provisioning();
  }
  while (is_provisioned && !is_provisioned() && prov_is_vbus_present()) {
    prov_run_serial_loop(timeout_ms, on_success);
    if (load_provisioning) load_provisioning();
  }
}

bool prov_write_and_verify(
    const char* path,
    const uint8_t key[16],
    uint32_t counter,
    CounterStore& store,
    uint8_t* runtime_key
) {
  InternalFS.remove(path);
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(path, Adafruit_LittleFS_Namespace::FILE_O_WRITE));
  if (!f) return false;

  f.write(reinterpret_cast<const uint8_t*>(&PROV_MAGIC), 4);
  f.write(key, 16);
  f.flush();
  f.close();

  // Verify readback
  Adafruit_LittleFS_Namespace::File fr(InternalFS.open(path, Adafruit_LittleFS_Namespace::FILE_O_READ));
  if (!fr || fr.size() < 20) return false;

  uint32_t read_magic = 0;
  uint8_t read_key[16];
  if (fr.read(reinterpret_cast<uint8_t*>(&read_magic), 4) != 4 ||
      read_magic != PROV_MAGIC ||
      fr.read(read_key, 16) != 16) {
    return false;
  }
  if (!constant_time_eq(read_key, key, 16)) return false;

  store.seed(counter);
  memcpy(runtime_key, key, 16);
  return true;
}

bool prov_load_key(const char* path, uint8_t out_key[16]) {
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(path, Adafruit_LittleFS_Namespace::FILE_O_READ));
  if (!f || f.size() < 20) return false;

  uint32_t magic = 0;
  if (f.read(reinterpret_cast<uint8_t*>(&magic), 4) != 4) return false;
  if (magic != PROV_MAGIC) return false;
  if (f.read(out_key, 16) != 16) return false;
  return true;
}

void prov_load_key_or_zero(const char* path, uint8_t out_key[16]) {
  if (!prov_load_key(path, out_key))
    memset(out_key, 0, 16);
}

}  // namespace immo
