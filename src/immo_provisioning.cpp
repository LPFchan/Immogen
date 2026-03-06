#include "immo_provisioning.h"
#include <Arduino.h>

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

uint16_t prov_parse_device_id(const char* dev_id, size_t len) {
  const char* p = dev_id;
  while (len > 0 && *p != '_') {
    p++;
    len--;
  }
  if (len == 0 || *p != '_') return 0xFFFF;
  p++;
  len--;
  unsigned int val = 0;
  for (; len > 0 && *p; p++, len--) {
    if (*p < '0' || *p > '9') return 0xFFFF;
    val = val * 10 + (unsigned int)(*p - '0');
  }
  return val > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(val);
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

bool prov_run_serial_loop(uint32_t timeout_ms, bool (*on_success)(uint16_t, const uint8_t[16], uint32_t)) {
  const uint32_t deadline = millis() + timeout_ms;
  char line[128];
  size_t len = 0;

  while (millis() < deadline && len < sizeof(line) - 1) {
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
      const char* col3 = col2 ? strchr(col2 + 1, ':') : nullptr;
      if (!col1 || !col2 || !col3) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      size_t dev_id_len = (size_t)(col1 - rest);
      const char* key_hex = col1 + 1;
      const char* counter_hex = col2 + 1;
      const char* checksum_hex = col3 + 1;
      
      if ((size_t)(col2 - key_hex) != 32 || (size_t)(col3 - counter_hex) != 8 || strlen(checksum_hex) < 4) {
        Serial.println("ERR:MALFORMED");
        return false;
      }

      uint16_t device_id = prov_parse_device_id(rest, dev_id_len);
      if (device_id == 0xFFFF) {
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

      if (on_success && on_success(device_id, key_buf, counter_val)) {
        Serial.println("ACK:PROV_SUCCESS");
        return true;
      }
      return false;
    }
    line[len++] = (char)c;
  }
  return false;
}

}  // namespace immo
