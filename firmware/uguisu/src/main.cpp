#include <Arduino.h>

#include <bluefruit.h>
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

// Runtime key and device_id: from Whimbrel provisioned flash or compile-time default.
uint8_t g_psk[16];
uint16_t g_device_id = UGUISU_DEVICE_ID;
bool g_has_provisioning = false;

immo::CounterStore g_store(COUNTER_LOG_PATH, OLD_COUNTER_LOG_PATH, COUNTER_LOG_MAX_BYTES);

// Callback for provisioning success
bool on_provision_success(uint16_t device_id, const uint8_t key[16], uint32_t counter) {
  InternalFS.remove(PROV_STORAGE_PATH);
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(PROV_STORAGE_PATH, FILE_O_WRITE));
  if (!f) return false;
  
  f.write(reinterpret_cast<const uint8_t*>(&PROV_MAGIC), 4);
  f.write(key, 16);
  f.write(reinterpret_cast<const uint8_t*>(&device_id), 2);
  f.write(reinterpret_cast<const uint8_t*>(&counter), 4);
  f.flush();
  f.close();

  // Verify
  Adafruit_LittleFS_Namespace::File fr(InternalFS.open(PROV_STORAGE_PATH, FILE_O_READ));
  if (!fr || fr.size() < 22) return false;
  
  uint32_t read_magic = 0;
  uint8_t read_key[16];
  uint16_t read_device_id = 0;
  if (fr.read(reinterpret_cast<uint8_t*>(&read_magic), 4) != 4 ||
      read_magic != PROV_MAGIC ||
      fr.read(read_key, 16) != 16 ||
      fr.read(reinterpret_cast<uint8_t*>(&read_device_id), 2) != 2) {
    return false;
  }
  
  if (!immo::constant_time_eq(read_key, key, 16) || read_device_id != device_id) {
    return false;
  }

  g_store.seed(device_id, counter);
  memcpy(g_psk, key, 16);
  g_device_id = device_id;
  g_has_provisioning = true;
  return true;
}

static bool key_is_all_zeros() {
  for (int i = 0; i < 16; i++)
    if (g_psk[i] != 0) return false;
  return true;
}

// Load key and device_id from /ug_prov.bin if present and valid; else use compile-time defaults.
static void load_provisioning() {
  Adafruit_LittleFS_Namespace::File f(InternalFS.open(PROV_STORAGE_PATH, FILE_O_READ));
  if (!f || f.size() < 26) {
    memcpy(g_psk, k_default_psk, 16);
    g_device_id = UGUISU_DEVICE_ID;
    g_has_provisioning = false;
    return;
  }
  uint32_t magic = 0;
  f.read(reinterpret_cast<uint8_t*>(&magic), 4);
  if (magic != PROV_MAGIC) {
    memcpy(g_psk, k_default_psk, 16);
    g_device_id = UGUISU_DEVICE_ID;
    g_has_provisioning = false;
    return;
  }
  if (f.read(g_psk, 16) != 16 || f.read(reinterpret_cast<uint8_t*>(&g_device_id), 2) != 2) {
    memcpy(g_psk, k_default_psk, 16);
    g_device_id = UGUISU_DEVICE_ID;
    g_has_provisioning = false;
    return;
  }
  g_has_provisioning = true;
}

void start_advertising_once(uint16_t company_id, const uint8_t payload11[immo::PAYLOAD_LEN]) {
  uint8_t msd[2 + immo::PAYLOAD_LEN];
  msd[0] = static_cast<uint8_t>(company_id & 0xFF);
  msd[1] = static_cast<uint8_t>((company_id >> 8) & 0xFF);
  memcpy(&msd[2], payload11, immo::PAYLOAD_LEN);

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

  load_provisioning();
  immo::ensure_provisioned(PROV_TIMEOUT_MS, on_provision_success, load_provisioning, key_is_all_zeros);

  Serial.println("BOOTED:Uguisu");
  Bluefruit.begin();
  Bluefruit.setName("Uguisu");
  Bluefruit.setTxPower(0);

  const uint16_t device_id = g_device_id;
  const uint32_t last = g_store.loadLast();
  const uint32_t counter = last + 1;
  const immo::Command command = immo::Command::Unlock;

  uint8_t nonce[immo::NONCE_LEN];
  immo::build_nonce(device_id, counter, nonce);

  uint8_t msg[immo::MSG_LEN];
  immo::build_msg(device_id, counter, command, msg);

  uint8_t mic[immo::MIC_LEN];
  const bool ok = immo::ccm_mic_4(g_psk, nonce, msg, sizeof(msg), mic);
  if (!ok) system_off();

  uint8_t payload11[immo::PAYLOAD_LEN];
  memcpy(&payload11[0], msg, sizeof(msg));
  memcpy(&payload11[sizeof(msg)], mic, sizeof(mic));

  start_advertising_once(static_cast<uint16_t>(UGUISU_COMPANY_ID), payload11);
  delay(UGUISU_ADVERTISE_MS);
  g_store.append(counter);
  system_off();
}

void loop() {}

