#include <Arduino.h>

#include <bluefruit.h>
#include <nrf_soc.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "guillemot_config.h"
#include "guillemot_secrets.h"

using namespace Adafruit_LittleFS_Namespace;

namespace {

static constexpr const char* COUNTER_LOG_PATH = "/ctr.log";
static constexpr size_t COUNTER_LOG_MAX_BYTES = 4096;

enum class Command : uint8_t {
  Unlock = 0x01,
  Lock = 0x02,
};

struct Payload {
  uint16_t device_id;
  uint32_t counter;
  Command command;
  uint8_t mic[MIC_LEN];
};

struct CounterRecord {
  uint16_t device_id;
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
  uint8_t buf[sizeof(r.device_id) + sizeof(r.counter)];
  memcpy(buf + 0, &r.device_id, sizeof(r.device_id));
  memcpy(buf + sizeof(r.device_id), &r.counter, sizeof(r.counter));
  return crc32_ieee(buf, sizeof(buf));
}

class CounterStore {
public:
  bool begin() {
    return InternalFS.begin();
  }

  void load() {
    last_device_id_ = 0;
    last_counter_ = 0;

    File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_READ));
    if (!f) return;

    CounterRecord rec{};
    while (f.read(reinterpret_cast<void*>(&rec), sizeof(rec)) == sizeof(rec)) {
      if (record_crc(rec) != rec.crc32) continue;
      last_device_id_ = rec.device_id;
      last_counter_ = rec.counter;
    }
  }

  uint32_t lastCounterFor(uint16_t device_id) const {
    if (device_id != last_device_id_) return 0;
    return last_counter_;
  }

  void update(uint16_t device_id, uint32_t counter) {
    rotateIfNeeded_();

    CounterRecord rec{};
    rec.device_id = device_id;
    rec.counter = counter;
    rec.crc32 = record_crc(rec);

    File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_WRITE | FILE_O_APPEND));
    if (!f) return;
    f.write(reinterpret_cast<const void*>(&rec), sizeof(rec));
    f.flush();

    last_device_id_ = device_id;
    last_counter_ = counter;
  }

private:
  void rotateIfNeeded_() {
    File f(InternalFS.open(COUNTER_LOG_PATH, FILE_O_READ));
    if (!f) return;
    const size_t sz = f.size();
    f.close();

    if (sz < COUNTER_LOG_MAX_BYTES) return;
    InternalFS.remove("/ctr.old");
    InternalFS.rename(COUNTER_LOG_PATH, "/ctr.old");
  }

  uint16_t last_device_id_{0};
  uint32_t last_counter_{0};
};

CounterStore g_store;

bool aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  nrf_ecb_hal_data_t ecb{};
  memcpy(ecb.key, key, 16);
  memcpy(ecb.cleartext, in, 16);

  const uint32_t err = sd_ecb_block_encrypt(&ecb);
  if (err != NRF_SUCCESS) return false;

  memcpy(out, ecb.ciphertext, 16);
  return true;
}

void xor_block(uint8_t dst[16], const uint8_t a[16], const uint8_t b[16]) {
  for (size_t i = 0; i < 16; i++) dst[i] = a[i] ^ b[i];
}

void ccm_build_nonce(uint16_t device_id, uint32_t counter, uint8_t nonce[13]) {
  nonce[0] = static_cast<uint8_t>(device_id & 0xFF);
  nonce[1] = static_cast<uint8_t>((device_id >> 8) & 0xFF);

  nonce[2] = static_cast<uint8_t>(counter & 0xFF);
  nonce[3] = static_cast<uint8_t>((counter >> 8) & 0xFF);
  nonce[4] = static_cast<uint8_t>((counter >> 16) & 0xFF);
  nonce[5] = static_cast<uint8_t>((counter >> 24) & 0xFF);

  for (size_t i = 6; i < 13; i++) nonce[i] = 0;
}

bool ccm_mic_4(const uint8_t key[16], const uint8_t nonce[13], const uint8_t* msg, size_t msg_len, uint8_t out_mic[4]) {
  if (msg_len > 0xFFFFu) return false;

  const uint8_t L = 2;
  const uint8_t M = 4;
  const uint8_t flags_b0 = static_cast<uint8_t>(((M - 2) / 2) << 3) | static_cast<uint8_t>(L - 1);

  uint8_t b0[16]{};
  b0[0] = flags_b0;
  memcpy(&b0[1], nonce, 13);
  b0[14] = static_cast<uint8_t>((msg_len >> 8) & 0xFF);
  b0[15] = static_cast<uint8_t>(msg_len & 0xFF);

  uint8_t x[16]{};
  uint8_t tmp[16]{};
  xor_block(tmp, x, b0);
  if (!aes128_ecb_encrypt(key, tmp, x)) return false;

  size_t offset = 0;
  while (offset < msg_len) {
    uint8_t block[16]{};
    const size_t n = min(static_cast<size_t>(16), msg_len - offset);
    memcpy(block, msg + offset, n);
    xor_block(tmp, x, block);
    if (!aes128_ecb_encrypt(key, tmp, x)) return false;
    offset += n;
  }

  uint8_t a0[16]{};
  a0[0] = static_cast<uint8_t>(L - 1);
  memcpy(&a0[1], nonce, 13);
  a0[14] = 0;
  a0[15] = 0;

  uint8_t s0[16]{};
  if (!aes128_ecb_encrypt(key, a0, s0)) return false;

  for (size_t i = 0; i < M; i++) out_mic[i] = static_cast<uint8_t>(x[i] ^ s0[i]);
  return true;
}

bool constant_time_eq(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; i++) diff |= (a[i] ^ b[i]);
  return diff == 0;
}

void latch_set_pulse() {
  digitalWrite(PIN_LATCH_RESET, LOW);
  digitalWrite(PIN_LATCH_SET, HIGH);
  delay(LATCH_PULSE_MS);
  digitalWrite(PIN_LATCH_SET, LOW);
}

void latch_reset_pulse() {
  digitalWrite(PIN_LATCH_SET, LOW);
  digitalWrite(PIN_LATCH_RESET, HIGH);
  delay(LATCH_PULSE_MS);
  digitalWrite(PIN_LATCH_RESET, LOW);
}

void buzzer_tone_ms(uint16_t duration_ms) {
  tone(PIN_BUZZER, BUZZER_HZ, duration_ms);
  delay(duration_ms);
  noTone(PIN_BUZZER);
}

bool parse_payload_from_report(ble_gap_evt_adv_report_t* report, Payload& out) {
  uint8_t msd[2 + PAYLOAD_LEN];
  const uint8_t len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, msd, sizeof(msd));
  if (len != sizeof(msd)) return false;

  const uint16_t company_id = static_cast<uint16_t>(msd[0] | (static_cast<uint16_t>(msd[1]) << 8));
  if (company_id != MSD_COMPANY_ID) return false;

  const uint8_t* p = msd + 2;
  out.device_id = static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
  out.counter = static_cast<uint32_t>(p[2] | (static_cast<uint32_t>(p[3]) << 8) | (static_cast<uint32_t>(p[4]) << 16) |
                                      (static_cast<uint32_t>(p[5]) << 24));
  out.command = static_cast<Command>(p[6]);
  memcpy(out.mic, p + 7, MIC_LEN);
  return true;
}

bool verify_payload(const Payload& pl) {
  uint8_t nonce[13];
  ccm_build_nonce(pl.device_id, pl.counter, nonce);

  uint8_t msg[2 + 4 + 1];
  msg[0] = static_cast<uint8_t>(pl.device_id & 0xFF);
  msg[1] = static_cast<uint8_t>((pl.device_id >> 8) & 0xFF);
  msg[2] = static_cast<uint8_t>(pl.counter & 0xFF);
  msg[3] = static_cast<uint8_t>((pl.counter >> 8) & 0xFF);
  msg[4] = static_cast<uint8_t>((pl.counter >> 16) & 0xFF);
  msg[5] = static_cast<uint8_t>((pl.counter >> 24) & 0xFF);
  msg[6] = static_cast<uint8_t>(pl.command);

  uint8_t expected[MIC_LEN];
  if (!ccm_mic_4(GUILLEMOT_PSK, nonce, msg, sizeof(msg), expected)) return false;
  return constant_time_eq(expected, pl.mic, MIC_LEN);
}

void handle_valid_command(const Payload& pl) {
  const uint32_t last = g_store.lastCounterFor(pl.device_id);
  if (pl.counter <= last) return;

  switch (pl.command) {
    case Command::Unlock:
      latch_set_pulse();
      buzzer_tone_ms(BUZZER_UNLOCK_MS);
      g_store.update(pl.device_id, pl.counter);
      break;
    case Command::Lock:
      buzzer_tone_ms(BUZZER_LOCK_MS);
      latch_reset_pulse();
      g_store.update(pl.device_id, pl.counter);
      break;
    default:
      break;
  }
}

void scan_callback(ble_gap_evt_adv_report_t* report) {
  Payload pl{};
  if (!parse_payload_from_report(report, pl)) return;
  if (!verify_payload(pl)) return;
  handle_valid_command(pl);
}

}  // namespace

void setup() {
  pinMode(PIN_LATCH_SET, OUTPUT);
  pinMode(PIN_LATCH_RESET, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_LATCH_SET, LOW);
  digitalWrite(PIN_LATCH_RESET, LOW);
  noTone(PIN_BUZZER);

  Serial.begin(115200);
  delay(50);

  if (!g_store.begin()) {
    Serial.println("InternalFS begin failed");
  }
  g_store.load();

  Bluefruit.begin(0, 1);
  Bluefruit.setName("Guillemot");

  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.filterMSD(MSD_COMPANY_ID);
  Bluefruit.Scanner.setIntervalMS(SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
  Bluefruit.Scanner.start(0);

  Serial.println("Guillemot scanning");
}

void loop() {
  sd_app_evt_wait();
}

