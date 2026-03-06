#include <Arduino.h>

#include <bluefruit.h>
#include <nrf_soc.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "uguisu_config.h"
#include "uguisu_protocol.h"
#include "uguisu_secrets.h"

using namespace Adafruit_LittleFS_Namespace;

namespace {

static constexpr const char* COUNTER_LOG_PATH = "/ug_ctr.log";
static constexpr const char* PROV_STORAGE_PATH = "/ug_prov.bin";
static constexpr size_t COUNTER_LOG_MAX_BYTES = 4096;
static constexpr size_t PROV_TIMEOUT_MS = 30000;
static constexpr size_t PROV_KEY_HEX_LEN = 32;
static constexpr size_t PROV_COUNTER_HEX_LEN = 8;
static constexpr uint32_t PROV_MAGIC = 0x76704755u;  // "UGpv" LE

// Runtime key and device_id: from Whimbrel provisioned flash or compile-time default.
uint8_t g_psk[16];
uint16_t g_device_id = UGUISU_DEVICE_ID;
bool g_has_provisioning = false;

struct CounterRecord {
  uint32_t counter;
  uint32_t crc32;
};

uint32_t crc32_ieee(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

uint32_t record_crc(const CounterRecord& r) {
  return crc32_ieee(reinterpret_cast<const uint8_t*>(&r.counter), sizeof(r.counter));
}

class CounterStore {
public:
  bool begin() { return InternalFS.begin(); }

  uint32_t loadLast() {
    uint32_t last = 0;
    Adafruit_LittleFS_Namespace::File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_READ));
    if (!f) return last;

    CounterRecord rec{};
    while (f.read(reinterpret_cast<void*>(&rec), sizeof(rec)) == sizeof(rec)) {
      if (record_crc(rec) != rec.crc32) continue;
      last = rec.counter;
    }
    return last;
  }

  void append(uint32_t counter) {
    rotateIfNeeded_();

    CounterRecord rec{};
    rec.counter = counter;
    rec.crc32 = record_crc(rec);

    Adafruit_LittleFS_Namespace::File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_WRITE));
    if (!f) return;
    f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
    f.flush();
  }

  // Clear log and write a single record (used after Whimbrel provisioning).
  void seed(uint32_t counter) {
    InternalFS.remove(COUNTER_LOG_PATH);
    InternalFS.remove("/ug_ctr.old");
    append(counter);
  }

private:
  void rotateIfNeeded_() {
    Adafruit_LittleFS_Namespace::File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_READ));
    if (!f) return;
    const size_t sz = f.size();
    f.close();

    if (sz < COUNTER_LOG_MAX_BYTES) return;
    InternalFS.remove("/ug_ctr.old");
    InternalFS.rename(COUNTER_LOG_PATH, "/ug_ctr.old");
  }
};

CounterStore g_store;

// --- Whimbrel provisioning (Web Serial PROV: line, 30s timeout) ---

static bool prov_is_vbus_present() {
#if defined(NRF52840_XXAA) || defined(NRF52833_XXAA)
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
#else
  return false;
#endif
}

static bool hex_byte(const char* hex, uint8_t* out) {
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

// Parse device_id string e.g. "UGUISU_01" -> 1. Returns 0xFFFF on parse error.
static uint16_t prov_parse_device_id(const char* dev_id, size_t len) {
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

// Parse 8 hex chars to uint32_t. Returns true on success.
static bool parse_counter_hex(const char* hex, uint32_t* out) {
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

// Wait for "PROV:DEVICE_ID:KEY_HEX:COUNTER_HEX". On success write /ug_prov.bin, seed counter log, send ACK, return true.
static bool prov_run_serial_loop() {
  const uint32_t deadline = millis() + PROV_TIMEOUT_MS;
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
      if (!col1 || !col2) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      size_t dev_id_len = (size_t)(col1 - rest);
      const char* key_hex = col1 + 1;
      const char* counter_hex = col2 + 1;
      size_t key_len = (size_t)(col2 - key_hex);
      size_t counter_len = strlen(counter_hex);
      if (key_len != PROV_KEY_HEX_LEN || counter_len != PROV_COUNTER_HEX_LEN) {
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

      InternalFS.remove(PROV_STORAGE_PATH);
      Adafruit_LittleFS_Namespace::File f(InternalFS.open(PROV_STORAGE_PATH, FILE_O_WRITE));
      if (!f) {
        Serial.println("ERR:MALFORMED");
        return false;
      }
      f.write(reinterpret_cast<const uint8_t*>(&PROV_MAGIC), 4);
      f.write(key_buf, 16);
      f.write(reinterpret_cast<const uint8_t*>(&device_id), 2);
      f.write(reinterpret_cast<const uint8_t*>(&counter_val), 4);
      f.flush();

      g_store.seed(counter_val);
      memcpy(g_psk, key_buf, 16);
      g_device_id = device_id;
      g_has_provisioning = true;
      Serial.println("ACK:PROV_SUCCESS");
      return true;
    }
    line[len++] = (char)c;
  }
  return false;
}

// Load key and device_id from /ug_prov.bin if present and valid; else use compile-time defaults.
static void load_provisioning() {
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(PROV_STORAGE_PATH, FILE_O_READ));
  if (!f || f.size() < 26) {
    memcpy(g_psk, UGUISU_PSK, 16);
    g_device_id = UGUISU_DEVICE_ID;
    g_has_provisioning = false;
    return;
  }
  uint32_t magic = 0;
  f.read(reinterpret_cast<uint8_t*>(&magic), 4);
  if (magic != PROV_MAGIC) {
    memcpy(g_psk, UGUISU_PSK, 16);
    g_device_id = UGUISU_DEVICE_ID;
    g_has_provisioning = false;
    return;
  }
  if (f.read(g_psk, 16) != 16 || f.read(reinterpret_cast<uint8_t*>(&g_device_id), 2) != 2) {
    memcpy(g_psk, UGUISU_PSK, 16);
    g_device_id = UGUISU_DEVICE_ID;
    g_has_provisioning = false;
    return;
  }
  g_has_provisioning = true;
}

void start_advertising_once(uint16_t company_id, const uint8_t payload11[uguisu::PAYLOAD_LEN]) {
  uint8_t msd[2 + uguisu::PAYLOAD_LEN];
  msd[0] = static_cast<uint8_t>(company_id & 0xFF);
  msd[1] = static_cast<uint8_t>((company_id >> 8) & 0xFF);
  memcpy(&msd[2], payload11, uguisu::PAYLOAD_LEN);

  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, msd, sizeof(msd));

  Bluefruit.Advertising.setInterval(UGUISU_ADV_INTERVAL_MS / 0.625, UGUISU_ADV_INTERVAL_MS / 0.625);
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.start(0);
}

void system_off() {
  Bluefruit.Advertising.stop();
  delay(10);
  sd_power_system_off();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(UGUISU_PIN_BUTTON, INPUT_PULLUP);

  if (!g_store.begin()) system_off();

  // Whimbrel provisioning: if powered over USB, wait up to 30s for PROV: line.
  if (prov_is_vbus_present()) {
    prov_run_serial_loop();
  }
  load_provisioning();

  Bluefruit.begin();
  Bluefruit.setName("Uguisu");
  Bluefruit.setTxPower(0);

  const uint16_t device_id = g_device_id;
  const uint32_t last = g_store.loadLast();
  const uint32_t counter = last + 1;
  const uguisu::Command command = uguisu::Command::Unlock;

  uint8_t nonce[uguisu::NONCE_LEN];
  uguisu::build_nonce(device_id, counter, nonce);

  uint8_t msg[uguisu::MSG_LEN];
  uguisu::build_msg(device_id, counter, command, msg);

  uint8_t mic[uguisu::MIC_LEN];
  const bool ok = uguisu::ccm_mic_4(g_psk, nonce, msg, sizeof(msg), mic);
  if (!ok) system_off();

  uint8_t payload11[uguisu::PAYLOAD_LEN];
  memcpy(&payload11[0], msg, sizeof(msg));
  memcpy(&payload11[sizeof(msg)], mic, sizeof(mic));

  start_advertising_once(static_cast<uint16_t>(UGUISU_COMPANY_ID), payload11);
  delay(UGUISU_ADVERTISE_MS);
  g_store.append(counter);
  system_off();
}

void loop() {}

