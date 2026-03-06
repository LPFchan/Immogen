#include <Arduino.h>

#include <bluefruit.h>
#include <nrf_gpio.h>
#include <nrf_soc.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "uguisu_config.h"
#include <ImmoCommon.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {

// Default PSK when not yet provisioned via Whimbrel (zeros).
static constexpr uint8_t k_default_psk[16] = {0};

static constexpr const char* COUNTER_LOG_PATH = "/ug_ctr.log";
static constexpr const char* OLD_COUNTER_LOG_PATH = "/ug_ctr.old";
static constexpr const char* PROV_STORAGE_PATH = "/ug_prov.bin";
static constexpr size_t COUNTER_LOG_MAX_BYTES = 4096;
static constexpr uint32_t PROV_TIMEOUT_MS = 30000;
static constexpr uint32_t PROV_MAGIC = 0x76704755u;  // "UGpv" LE

// Runtime key: from Whimbrel provisioned flash or compile-time default.
uint8_t g_psk[16];
bool g_has_provisioning = false;

immo::CounterStore g_store(COUNTER_LOG_PATH, OLD_COUNTER_LOG_PATH, COUNTER_LOG_MAX_BYTES);

// Callback for provisioning success
bool on_provision_success(const uint8_t key[16], uint32_t counter) {
  InternalFS.remove(PROV_STORAGE_PATH);
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(PROV_STORAGE_PATH, FILE_O_WRITE));
  if (!f) return false;
  
  f.write(reinterpret_cast<const uint8_t*>(&PROV_MAGIC), 4);
  f.write(key, 16);
  f.flush();
  f.close();

  // Verify
  Adafruit_LittleFS_Namespace::File fr(InternalFS.open(PROV_STORAGE_PATH, FILE_O_READ));
  if (!fr || fr.size() < 20) return false;
  
  uint32_t read_magic = 0;
  uint8_t read_key[16];
  if (fr.read(reinterpret_cast<uint8_t*>(&read_magic), 4) != 4 ||
      read_magic != PROV_MAGIC ||
      fr.read(read_key, 16) != 16) {
    return false;
  }
  
  if (!immo::constant_time_eq(read_key, key, 16)) {
    return false;
  }

  g_store.seed(counter);
  memcpy(g_psk, key, 16);
  g_has_provisioning = true;
  return true;
}

static bool key_is_all_zeros() {
  for (int i = 0; i < 16; i++)
    if (g_psk[i] != 0) return false;
  return true;
}

// Load key from /ug_prov.bin if present and valid; else use compile-time default.
static void load_provisioning() {
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(PROV_STORAGE_PATH, FILE_O_READ));
  if (!f || f.size() < 20) {
    memcpy(g_psk, k_default_psk, 16);
    g_has_provisioning = false;
    return;
  }
  uint32_t magic = 0;
  f.read(reinterpret_cast<uint8_t*>(&magic), 4);
  if (magic != PROV_MAGIC) {
    memcpy(g_psk, k_default_psk, 16);
    g_has_provisioning = false;
    return;
  }
  if (f.read(g_psk, 16) != 16) {
    memcpy(g_psk, k_default_psk, 16);
    g_has_provisioning = false;
    return;
  }
  g_has_provisioning = true;
}

void start_advertising_once(uint16_t company_id, const uint8_t payload9[immo::PAYLOAD_LEN]) {
  uint8_t msd[2 + immo::PAYLOAD_LEN];
  msd[0] = static_cast<uint8_t>(company_id & 0xFF);
  msd[1] = static_cast<uint8_t>((company_id >> 8) & 0xFF);
  memcpy(&msd[2], payload9, immo::PAYLOAD_LEN);

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
#ifdef UGUISU_PIN_BUTTON_NRF
  nrf_gpio_cfg_sense_input(UGUISU_PIN_BUTTON_NRF, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif
  sd_power_system_off();
}

// Waits for button press and release, returns press duration in ms.
// Button is active LOW (INPUT_PULLUP). Returns 0 if timeout.
static uint32_t wait_for_button_press_release(uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;
  while (millis() < deadline && digitalRead(UGUISU_PIN_BUTTON) != LOW) {
    delay(10);
  }
  if (digitalRead(UGUISU_PIN_BUTTON) != LOW) return 0;

  const uint32_t press_start = millis();
  while (millis() < deadline && digitalRead(UGUISU_PIN_BUTTON) == LOW) {
    delay(10);
  }
  return millis() - press_start;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(UGUISU_PIN_BUTTON, INPUT_PULLUP);

  if (!g_store.begin()) system_off();

  load_provisioning();
  immo::ensure_provisioned(PROV_TIMEOUT_MS, on_provision_success, load_provisioning, key_is_all_zeros);

  Serial.println("BOOTED:Uguisu");
  Bluefruit.begin();
  Bluefruit.setName("Uguisu");
  Bluefruit.setTxPower(0);

  // Wait for button: single press = Unlock, long press (>= 2s) = Lock
  const uint32_t press_ms = wait_for_button_press_release(10000);
  if (press_ms == 0) system_off();  // No press within timeout, sleep
  const immo::Command command =
      (press_ms >= UGUISU_LONG_PRESS_MS) ? immo::Command::Lock : immo::Command::Unlock;

  const uint32_t last = g_store.loadLast();
  const uint32_t counter = last + 1;

  uint8_t nonce[immo::NONCE_LEN];
  immo::build_nonce(counter, nonce);

  uint8_t msg[immo::MSG_LEN];
  immo::build_msg(counter, command, msg);

  uint8_t mic[immo::MIC_LEN];
  const bool ok = immo::ccm_mic_4(g_psk, nonce, msg, sizeof(msg), mic);
  if (!ok) system_off();

  uint8_t payload9[immo::PAYLOAD_LEN];
  memcpy(&payload9[0], msg, sizeof(msg));
  memcpy(&payload9[sizeof(msg)], mic, sizeof(mic));

  start_advertising_once(static_cast<uint16_t>(UGUISU_COMPANY_ID), payload9);
  delay(UGUISU_ADVERTISE_MS);
  g_store.update(counter);
  system_off();
}

void loop() {}

