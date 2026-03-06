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
static constexpr size_t COUNTER_LOG_MAX_BYTES = 4096;

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
    File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_READ));
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

    File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_WRITE));
    if (!f) return;
    f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec));
    f.flush();
  }

private:
  void rotateIfNeeded_() {
    File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_READ));
    if (!f) return;
    const size_t sz = f.size();
    f.close();

    if (sz < COUNTER_LOG_MAX_BYTES) return;
    InternalFS.remove("/ug_ctr.old");
    InternalFS.rename(COUNTER_LOG_PATH, "/ug_ctr.old");
  }
};

CounterStore g_store;

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
  // Keep serial optional; do not block on it.
  Serial.begin(115200);

  pinMode(UGUISU_PIN_BUTTON, INPUT_PULLUP);

  if (!g_store.begin()) system_off();

  Bluefruit.begin();
  Bluefruit.setName("Uguisu");
  Bluefruit.setTxPower(0);

  const uint16_t device_id = static_cast<uint16_t>(UGUISU_DEVICE_ID);
  const uint32_t last = g_store.loadLast();
  const uint32_t counter = last + 1;
  const uguisu::Command command = uguisu::Command::Unlock;

  uint8_t nonce[uguisu::NONCE_LEN];
  uguisu::build_nonce(device_id, counter, nonce);

  uint8_t msg[uguisu::MSG_LEN];
  uguisu::build_msg(device_id, counter, command, msg);

  uint8_t mic[uguisu::MIC_LEN];
  const bool ok = uguisu::ccm_mic_4(UGUISU_PSK, nonce, msg, sizeof(msg), mic);
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

