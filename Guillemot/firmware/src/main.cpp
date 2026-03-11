#include <Arduino.h>

#include <bluefruit.h>
#include <nrf_soc.h>
#include <nrf_wdt.h>

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "guillemot_config.h"
#include <ImmoCommon.h>
#include <ArduinoJson.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {

static constexpr const char* COUNTER_LOG_PATH = "/ctr.log";
static constexpr const char* OLD_COUNTER_LOG_PATH = "/ctr.old";

// PIN rate limiting
static constexpr uint32_t PIN_LOCKOUT_MS = 60 * 60 * 1000; // 1 hour
uint8_t g_pin_failures = 0;
uint32_t g_pin_lockout_start = 0;

// Hardware state
bool g_locked = true;

// Session identity tracking
int8_t g_session_slot = -1; // -1 if not identified, 0-3 for key slot

// Runtime key: loaded from flash (Whimbrel-provisioned) or compile-time default.
uint8_t g_psk[16];
char g_smp_pin[7] = "000000";

immo::CounterStore g_store(COUNTER_LOG_PATH, OLD_COUNTER_LOG_PATH, immo::DEFAULT_COUNTER_LOG_MAX_BYTES);

// BLE GATT Services & Characteristics
BLEService proximityService("942C7A1E-362E-4676-A22F-39130FAF2272");
BLECharacteristic unlockLockChr("2522DA08-9E21-47DB-A834-22B7267E178B");
BLECharacteristic mgmtCmdChr("438C5641-3825-40BE-80A8-97BC261E0EE9");
BLECharacteristic mgmtRspChr("DA43E428-803C-401B-9915-4C1529F453B1");

// iBeacon UUID
uint8_t iBeaconUuid[16] = {
  0x66, 0x96, 0x2B, 0x67, 0x9C, 0x59, 0x4D, 0x83,
  0x91, 0x01, 0xAC, 0x0C, 0x9C, 0xCA, 0x2B, 0x12
};
BLEBeacon iBeacon(iBeaconUuid, 0x0000, 0x0000, -54);

// Proximity Beacon UUIDs for Scan Response
uint8_t uuidLocked[16] = {
  0xC5, 0x38, 0x0E, 0xF2, 0xC3, 0xFC, 0x4F, 0x2A,
  0xB3, 0xCC, 0xD5, 0x1A, 0x08, 0xEF, 0x5F, 0xA9
};
uint8_t uuidUnlocked[16] = {
  0xA1, 0xAA, 0x4F, 0x79, 0xB4, 0x90, 0x44, 0xD2,
  0xA7, 0xE1, 0x8A, 0x03, 0x42, 0x22, 0x43, 0xA1
};
BLEUuid bleUuidLocked(uuidLocked);
BLEUuid bleUuidUnlocked(uuidUnlocked);

void update_advertising();

bool on_provision_success(const uint8_t key[16], uint32_t counter) {
  return immo::prov_write_and_verify(immo::DEFAULT_PROV_PATH, key, counter, g_store, g_psk);
}

static bool key_is_all_zeros() { return immo::is_key_blank(g_psk); }

static void load_psk_from_storage() {
  immo::prov_load_key_or_zero(immo::DEFAULT_PROV_PATH, g_psk);
  // NOTE: Assuming PIN is also stored or using a default. For simplicity, reading PIN from flash if possible,
  // but if SETPIN serial command is only way to set it, we might need to store it.
  // Wait, the requirements say "The 6-digit PIN established during USB setup serves as the standard BLE Pairing PIN".
  // I will add a simple file read for PIN.
  File f = InternalFS.open("/pin.txt", FILE_O_READ);
  if (f) {
    f.read(g_smp_pin, 6);
    g_smp_pin[6] = 0;
    f.close();
  }
}

void buzzer_tone_ms(uint16_t hz, uint16_t duration_ms) {
  tone(PIN_BUZZER, hz, duration_ms);
  delay(duration_ms);
  noTone(PIN_BUZZER);
}

bool parse_payload_from_report(ble_gap_evt_adv_report_t* report, uint8_t ct[immo::MSG_LEN], uint8_t mic[immo::MIC_LEN]) {
  uint8_t msd[4 + immo::PAYLOAD_LEN];
  const uint8_t len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, msd, sizeof(msd));
  if (len != sizeof(msd)) return false;

  const uint16_t company_id = static_cast<uint16_t>(msd[0] | (static_cast<uint16_t>(msd[1]) << 8));
  if (company_id != MSD_COMPANY_ID) return false;

  const uint16_t magic = static_cast<uint16_t>(msd[2] | (static_cast<uint16_t>(msd[3]) << 8));
  if (magic != immo::IMMOGEN_MAGIC) return false;

  const uint8_t* p = msd + 4;
  memcpy(ct, p, immo::MSG_LEN);
  memcpy(mic, p + immo::MSG_LEN, immo::MIC_LEN);
  return true;
}

bool verify_payload(const uint8_t ct[immo::MSG_LEN], const uint8_t mic[immo::MIC_LEN], immo::Payload& out_pl) {
  // Extract prefix and counter (first 5 bytes of ct, which are unencrypted AAD)
  const uint8_t prefix = ct[0];
  const uint32_t counter = static_cast<uint32_t>(ct[1] | (static_cast<uint32_t>(ct[2]) << 8) | (static_cast<uint32_t>(ct[3]) << 16) | (static_cast<uint32_t>(ct[4]) << 24));

  uint8_t nonce[immo::NONCE_LEN];
  immo::build_nonce(counter, nonce);

  uint8_t msg[immo::MSG_LEN];
  uint8_t expected[immo::MIC_LEN];
  // 5 bytes AAD: prefix (1 byte) + counter (4 bytes)
  if (!immo::ccm_auth_decrypt(g_psk, nonce, ct, immo::MSG_LEN, 5, msg, expected)) return false;

  if (!immo::constant_time_eq(expected, mic, immo::MIC_LEN)) return false;

  out_pl.prefix = prefix;
  out_pl.counter = counter;
  out_pl.command = static_cast<immo::Command>(msg[5]);
  memcpy(out_pl.mic, mic, immo::MIC_LEN);
  return true;
}

void handle_valid_command(const immo::Payload& pl) {
  const uint32_t last = g_store.lastCounter();
  if (pl.counter <= last) return;

  g_store.update(pl.counter);

  // Valid AES-CCM Lock/Unlock instantly resets PIN failure counter
  if (pl.command == immo::Command::Lock || pl.command == immo::Command::Unlock) {
      g_pin_failures = 0;
      g_pin_lockout_start = 0;
  }

  bool state_changed = false;

  switch (pl.command) {
    case immo::Command::Unlock:
      if (g_locked) {
          latch_set_pulse();
          buzzer_tone_ms(BUZZER_LOW_HZ,  BUZZER_LOW_MS);
          buzzer_tone_ms(BUZZER_HIGH_HZ, BUZZER_HIGH_MS);
          g_locked = false;
          state_changed = true;
      }
      break;
    case immo::Command::Lock:
      if (!g_locked) {
          buzzer_tone_ms(BUZZER_HIGH_HZ, BUZZER_HIGH_MS);
          buzzer_tone_ms(BUZZER_LOW_HZ,  BUZZER_LOW_MS);
          latch_reset_pulse();
          g_locked = true;
          state_changed = true;
      }
      break;
    case immo::Command::Identify:
      g_session_slot = (pl.prefix >> 4) & 0x03;
      break;
    default:
      // Ignore unknown commands
      break;
  }

  if (state_changed) {
      update_advertising();
  }
}

void scan_callback(ble_gap_evt_adv_report_t* report) {
  uint8_t ct[immo::MSG_LEN];
  uint8_t mic[immo::MIC_LEN];
  if (!parse_payload_from_report(report, ct, mic)) return;
  if (key_is_all_zeros()) return;
  
  immo::Payload pl{};
  if (!verify_payload(ct, mic, pl)) return;
  handle_valid_command(pl);
}

void unlock_lock_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    if (len != immo::PAYLOAD_LEN) return;
    uint8_t ct[immo::MSG_LEN];
    uint8_t mic[immo::MIC_LEN];
    memcpy(ct, data, immo::MSG_LEN);
    memcpy(mic, data + immo::MSG_LEN, immo::MIC_LEN);
    
    immo::Payload pl{};
    if (!verify_payload(ct, mic, pl)) return;
    handle_valid_command(pl);
}

void send_json_response(const JsonDocument& doc) {
    char buf[247];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (Bluefruit.connected() && mgmtRspChr.notifyEnabled()) {
        mgmtRspChr.notify(buf, len);
    }
    Serial.println(buf);
}

void process_management_command(String cmd, bool is_serial) {
    StaticJsonDocument<256> rsp;
    rsp["status"] = "error";

    if (cmd.startsWith("SETPIN:") && is_serial) {
        String pinStr = cmd.substring(7);
        pinStr.trim();
        if (pinStr.length() == 6) {
            strncpy(g_smp_pin, pinStr.c_str(), 6);
            g_smp_pin[6] = 0;
            Bluefruit.Security.setPIN(g_smp_pin);
            File f = InternalFS.open("/pin.txt", FILE_O_WRITE);
            if (f) {
                f.write(g_smp_pin, 6);
                f.close();
            }
            rsp["status"] = "ok";
        } else {
            rsp["reason"] = "invalid_pin";
        }
    } else if (cmd == "RESETLOCK" && is_serial) {
        InternalFS.format();
        rsp["status"] = "ok";
        send_json_response(rsp);
        delay(100);
        NVIC_SystemReset();
    } else if (cmd.startsWith("PROV:")) {
        rsp["reason"] = "not_implemented";
    } else if (cmd.startsWith("RENAME:")) {
        rsp["reason"] = "not_implemented";
    } else if (cmd == "SLOTS?") {
        rsp["reason"] = "not_implemented";
    } else if (cmd.startsWith("REVOKE:")) {
        rsp["reason"] = "not_implemented";
    } else if (cmd.startsWith("RECOVER:")) {
        rsp["reason"] = "not_implemented";
    } else {
        rsp["reason"] = "unknown_command";
    }

    send_json_response(rsp);
}

void mgmt_cmd_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    // Authenticated link check (assuming characteristic properties enforce this)
    if (g_session_slot == -1) return; // Must be IDENTIFY'd first
    
    char buf[248];
    uint16_t copy_len = len < 247 ? len : 247;
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\0';
    String cmd(buf);
    process_management_command(cmd, false);
}

void prph_connect_callback(uint16_t conn_handle) {
    g_session_slot = -1; // Reset identity on connection
}

void prph_disconnect_callback(uint16_t conn_handle, uint8_t reason) {
    g_session_slot = -1;
}

void update_advertising() {
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();
    
    Bluefruit.Advertising.setBeacon(iBeacon);

    // Scan Response packet for Proximity UUID
    if (g_locked) {
        Bluefruit.ScanResponse.addUuid(bleUuidLocked);
        Bluefruit.Advertising.setInterval(480, 480); // 300 ms (480 * 0.625)
    } else {
        Bluefruit.ScanResponse.addUuid(bleUuidUnlocked);
        Bluefruit.Advertising.setInterval(320, 320); // 200 ms (320 * 0.625)
    }
    
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.start(0);
}

void pairing_complete_callback(uint16_t conn_handle, uint8_t auth_status) {
    if (auth_status != BLE_GAP_SEC_STATUS_SUCCESS) {
        g_pin_failures++;
        if (g_pin_failures >= 10) {
            g_pin_lockout_start = millis();
        }
    } else {
        g_pin_failures = 0;
        g_pin_lockout_start = 0;
    }
}

bool pairing_passkey_callback(uint16_t conn_handle, uint8_t const passkey[6], bool match_request) {
    return true;
}

void ble_sec_cb(uint16_t conn_handle) {
    g_pin_failures = 0;
    g_pin_lockout_start = 0;
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
  immo::ensure_provisioned(immo::DEFAULT_PROV_TIMEOUT_MS, on_provision_success, load_psk_from_storage, key_is_all_zeros);

  g_store.load();

  // Dual Role: 1 Peripheral, 1 Central
  Bluefruit.begin(1, 1);
  Bluefruit.setName("Guillemot");
  Bluefruit.setTxPower(4);

  Bluefruit.Periph.setConnectCallback(prph_connect_callback);
  Bluefruit.Periph.setDisconnectCallback(prph_disconnect_callback);

  // Security setup
  Bluefruit.Security.setPairCompleteCallback(pairing_complete_callback);
  Bluefruit.Security.setSecuredCallback(ble_sec_cb);
  Bluefruit.Security.setPIN(g_smp_pin);
  Bluefruit.Security.setPairPasskeyCallback(pairing_passkey_callback);

  // Setup Proximity Service
  proximityService.begin();

  // Setup Unlock/Lock Command Characteristic (Write Without Response)
  unlockLockChr.setProperties(CHR_PROPS_WRITE_WO_RESP);
  unlockLockChr.setPermission(SECMODE_OPEN, SECMODE_OPEN); // Payload is AES-CCM encrypted
  unlockLockChr.setWriteCallback(unlock_lock_write_callback);
  unlockLockChr.begin();

  // Setup Management Command Characteristic (Write, Authenticated)
  mgmtCmdChr.setProperties(CHR_PROPS_WRITE);
  mgmtCmdChr.setPermission(SECMODE_OPEN, SECMODE_ENC_WITH_MITM); // Authenticated link via SMP
  mgmtCmdChr.setWriteCallback(mgmt_cmd_write_callback);
  mgmtCmdChr.begin();

  // Setup Management Response Characteristic (Notify)
  mgmtRspChr.setProperties(CHR_PROPS_NOTIFY);
  mgmtRspChr.setPermission(SECMODE_OPEN, SECMODE_ENC_WITH_MITM);
  mgmtRspChr.begin();

  // Configure Scanner
  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.filterMSD(MSD_COMPANY_ID);
  Bluefruit.Scanner.setIntervalMS(SCAN_INTERVAL_MS, SCAN_WINDOW_MS);
  Bluefruit.Scanner.start(0);

  // Configure Advertiser
  iBeacon.setManufacturer(0x004C); // Apple
  update_advertising();

  nrf_wdt_behaviour_set(NRF_WDT, NRF_WDT_BEHAVIOUR_RUN_SLEEP);
  nrf_wdt_reload_value_set(NRF_WDT, (8000 * 32768) / 1000); // 8 second timeout
  nrf_wdt_reload_request_enable(NRF_WDT, NRF_WDT_RR0);
  nrf_wdt_task_trigger(NRF_WDT, NRF_WDT_TASK_START);

  Serial.println("Guillemot running");
  Serial.print("BOOTED: Guillemot-");
  Serial.println(__TIMESTAMP__);
  Serial.print("HWID: ");
  Serial.print(NRF_FICR->DEVICEID[0], HEX);
  Serial.println(NRF_FICR->DEVICEID[1], HEX);
}

void loop() {
  nrf_wdt_reload_request_set(NRF_WDT, NRF_WDT_RR0);
  sd_app_evt_wait();

  // Process Serial Commands
  if (Serial.available()) {
      String cmd = Serial.readStringUntil('\n');
      cmd.trim();
      if (cmd.length() > 0) {
          process_management_command(cmd, true);
      }
  }
}
