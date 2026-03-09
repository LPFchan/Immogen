#include <Arduino.h>

#include <bluefruit.h>
#include <nrf_soc.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "guillemot_config.h"
#include <ImmoCommon.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {

// Default PSK when not yet provisioned via Whimbrel (zeros).
static constexpr uint8_t k_default_psk[16] = {0};

static constexpr const char* COUNTER_LOG_PATH = "/ctr.log";
static constexpr const char* OLD_COUNTER_LOG_PATH = "/ctr.old";
static constexpr const char* PROV_STORAGE_PATH = "/prov.bin";
static constexpr size_t COUNTER_LOG_MAX_BYTES = 4096;
static constexpr uint32_t PROV_TIMEOUT_MS = 30000;

// Runtime key: loaded from flash (Whimbrel-provisioned) or compile-time default.
uint8_t g_psk[16];

immo::CounterStore g_store(COUNTER_LOG_PATH, OLD_COUNTER_LOG_PATH, COUNTER_LOG_MAX_BYTES);

bool on_provision_success(const uint8_t key[16], uint32_t counter) {
  return immo::prov_write_and_verify(PROV_STORAGE_PATH, key, counter, g_store, g_psk);
}

static bool key_is_all_zeros() { return immo::is_key_blank(g_psk); }

// Load g_psk from flash if provisioned, else use compile-time default.
static void load_psk_from_storage() {
  if (!immo::prov_load_key(PROV_STORAGE_PATH, g_psk))
    memcpy(g_psk, k_default_psk, 16);
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

bool parse_payload_from_report(ble_gap_evt_adv_report_t* report, immo::Payload& out) {
  uint8_t msd[2 + immo::PAYLOAD_LEN];
  const uint8_t len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, msd, sizeof(msd));
  if (len != sizeof(msd)) return false;

  const uint16_t company_id = static_cast<uint16_t>(msd[0] | (static_cast<uint16_t>(msd[1]) << 8));
  if (company_id != MSD_COMPANY_ID) return false;

  const uint8_t* p = msd + 2;
  out.counter = static_cast<uint32_t>(p[0] | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                                      (static_cast<uint32_t>(p[3]) << 24));
  out.command = static_cast<immo::Command>(p[4]);
  memcpy(out.mic, p + 5, immo::MIC_LEN);
  return true;
}

bool verify_payload(const immo::Payload& pl) {
  uint8_t nonce[immo::NONCE_LEN];
  immo::build_nonce(pl.counter, nonce);

  uint8_t msg[immo::MSG_LEN];
  immo::build_msg(pl.counter, pl.command, msg);

  uint8_t expected[immo::MIC_LEN];
  if (!immo::ccm_mic_8(g_psk, nonce, msg, sizeof(msg), expected)) return false;
  return immo::constant_time_eq(expected, pl.mic, immo::MIC_LEN);
}

void handle_valid_command(const immo::Payload& pl) {
  const uint32_t last = g_store.lastCounter();
  if (pl.counter <= last) return;

  g_store.update(pl.counter);

  switch (pl.command) {
    case immo::Command::Unlock:
      latch_set_pulse();
      buzzer_tone_ms(BUZZER_UNLOCK_MS);
      break;
    case immo::Command::Lock:
      buzzer_tone_ms(BUZZER_LOCK_MS);
      latch_reset_pulse();
      break;
    default:
      // Ignore unknown commands
      break;
  }
}

void scan_callback(ble_gap_evt_adv_report_t* report) {
  immo::Payload pl{};
  if (!parse_payload_from_report(report, pl)) return;
  if (key_is_all_zeros()) return;
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
    immo::led_error_loop(PIN_ERROR_LED);
  }

  load_psk_from_storage();
  immo::ensure_provisioned(PROV_TIMEOUT_MS, on_provision_success, load_psk_from_storage, key_is_all_zeros);

  g_store.load();

  Bluefruit.begin(0, 1);
  Bluefruit.setName("Guillemot");

  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.filterMSD(MSD_COMPANY_ID);
  Bluefruit.Scanner.setIntervalMS(SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
  Bluefruit.Scanner.start(0);

  Serial.println("Guillemot scanning");
  Serial.println("BOOTED:Guillemot");
}

void loop() {
  sd_app_evt_wait();
}
