#include <Arduino.h>

#include <bluefruit.h>
#include <nrf_gpio.h>
#include <nrf_soc.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "uguisu_config.h"
#include "led_effects.h"
#include <ImmoCommon.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {

static constexpr const char* COUNTER_LOG_PATH = "/ug_ctr.log";
static constexpr const char* OLD_COUNTER_LOG_PATH = "/ug_ctr.old";

// Runtime key: from Whimbrel provisioned flash or compile-time default.
uint8_t g_psk[16];

immo::CounterStore g_store(COUNTER_LOG_PATH, OLD_COUNTER_LOG_PATH, immo::DEFAULT_COUNTER_LOG_MAX_BYTES);

// Callback for provisioning success
bool on_provision_success(const uint8_t key[16], uint32_t counter) {
  return immo::prov_write_and_verify(immo::DEFAULT_PROV_PATH, key, counter, g_store, g_psk);
}

static bool key_is_all_zeros() { return immo::is_key_blank(g_psk); }

static void load_provisioning() {
  immo::prov_load_key_or_zero(immo::DEFAULT_PROV_PATH, g_psk);
}

static TaskHandle_t g_prov_led_task = nullptr;
static void prov_led_task(void*) {
  while (true) { led::prov_pulse(); }
}

void start_advertising_once(uint16_t company_id, const uint8_t payload13[immo::PAYLOAD_LEN]) {
  uint8_t msd[2 + immo::PAYLOAD_LEN];
  msd[0] = static_cast<uint8_t>(company_id & 0xFF);
  msd[1] = static_cast<uint8_t>((company_id >> 8) & 0xFF);
  memcpy(&msd[2], payload13, immo::PAYLOAD_LEN);

  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addName();

  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, msd, sizeof(msd));

  Bluefruit.Advertising.setInterval((UGUISU_ADV_INTERVAL_MS * 8 + 4) / 5, (UGUISU_ADV_INTERVAL_MS * 8 + 4) / 5);
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

  led::init();
  pinMode(UGUISU_PIN_BUTTON, INPUT_PULLUP);

  if (!g_store.begin()) {
    Serial.println("InternalFS begin failed");
    led::error_loop(PIN_ERROR_LED);
  }

  load_provisioning();
  if (immo::prov_is_vbus_present()) {
    xTaskCreate(prov_led_task, "prov_led", 128, nullptr, 1, &g_prov_led_task);
  }
  immo::ensure_provisioned(immo::DEFAULT_PROV_TIMEOUT_MS, on_provision_success, load_provisioning, key_is_all_zeros);
  if (g_prov_led_task) {
    vTaskDelete(g_prov_led_task);
    g_prov_led_task = nullptr;
  }
  led::off();

  Serial.println("BOOTED:Uguisu");
  Bluefruit.begin();
  Bluefruit.setName("Uguisu");
  Bluefruit.setTxPower(0);

  // Wait for button: single press = Unlock, long press (>= 1s) = Lock
  const uint32_t press_ms = wait_for_button_press_release(UGUISU_BUTTON_TIMEOUT_MS);
  if (press_ms == 0) system_off();  // No press within timeout, sleep
  const immo::Command command =
      (press_ms >= UGUISU_LONG_PRESS_MS) ? immo::Command::Lock : immo::Command::Unlock;
  const uint8_t cmd_pin = (command == immo::Command::Lock) ? PIN_LED_R : PIN_LED_G;
  const bool low_bat = (readVbat_mv() < LED_LOWBAT_MV_THRESHOLD);

  const uint32_t last = g_store.loadLast();
  const uint32_t counter = last + 1;

  uint8_t nonce[immo::NONCE_LEN];
  immo::build_nonce(counter, nonce);

  uint8_t msg[immo::MSG_LEN];
  immo::build_msg(counter, command, msg);

  uint8_t mic[immo::MIC_LEN];
  const bool ok = immo::ccm_mic_8(g_psk, nonce, msg, sizeof(msg), mic);
  if (!ok) system_off();

  uint8_t payload13[immo::PAYLOAD_LEN];
  memcpy(&payload13[0], msg, sizeof(msg));
  memcpy(&payload13[sizeof(msg)], mic, sizeof(mic));

  g_store.update(counter);
  start_advertising_once(MSD_COMPANY_ID, payload13);
  const uint32_t adv_start = millis();

  if (low_bat) {
    led::flash_low_battery(cmd_pin);
  } else {
    led::flash_once(cmd_pin, LED_FLASH_RISE_MS, LED_FLASH_HOLD_MS, LED_FLASH_FALL_MS);
  }

  const uint32_t elapsed = millis() - adv_start;
  if (elapsed < UGUISU_ADVERTISE_MS) {
    delay(UGUISU_ADVERTISE_MS - elapsed);
  }
  system_off();
}

void loop() {}

