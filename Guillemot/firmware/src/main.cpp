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

// Window state
uint32_t g_window_end_ms = 0;
bool g_window_active = false;
uint32_t g_window_remaining_ms = 0; // For pausing during GATT connection
static constexpr uint32_t WINDOW_DURATION_MS = 30000;

// Forward declaration
void send_json_response(const JsonDocument& doc);
void process_management_command(String cmd, bool is_serial, bool is_authenticated = false);

// Session identity tracking
int8_t g_session_slot = -1; // -1 if not identified, 0-3 for key slot

// Runtime keys: loaded from flash (Whimbrel-provisioned)
immo::KeySlot g_slots[immo::MAX_KEY_SLOTS];
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
uint8_t uuidWindow[16] = {
  0xB9, 0x9F, 0x8D, 0x62, 0xA1, 0xC3, 0x4E, 0x8B,
  0x9D, 0x2F, 0x5C, 0x3A, 0x1B, 0x4E, 0x6D, 0x7A
};
BLEUuid bleUuidLocked(uuidLocked);
BLEUuid bleUuidUnlocked(uuidUnlocked);
BLEUuid bleUuidWindow(uuidWindow);

void update_advertising();

bool on_provision_success(const uint8_t key[16], uint32_t counter) {
  // Save Slot 1 via new method
  memcpy(g_slots[1].aes_key, key, 16);
  g_slots[1].counter = counter;
  strncpy(g_slots[1].name, "Owner's Phone", sizeof(g_slots[1].name) - 1);
  
  File f = InternalFS.open("/slot1.dat", FILE_O_WRITE);
  if (f) {
      f.write(g_slots[1].aes_key, 16);
      f.write((uint8_t*)&g_slots[1].counter, 4);
      f.write(g_slots[1].name, sizeof(g_slots[1].name));
      f.close();
  }

  return immo::prov_write_and_verify(immo::DEFAULT_PROV_PATH, key, counter, g_store, g_slots[1].aes_key);
}

static bool key_is_all_zeros(const uint8_t key[16]) {
  for (int i = 0; i < 16; i++) {
    if (key[i] != 0) return false;
  }
  return true;
}

static bool slot_1_is_empty() {
  return key_is_all_zeros(g_slots[1].aes_key);
}

static void load_psk_from_storage() {
  memset(g_slots, 0, sizeof(g_slots));
  
  // Load each slot from LittleFS
  for (int i=0; i<immo::MAX_KEY_SLOTS; i++) {
      String filePath = String("/slot") + i + ".dat";
      File f = InternalFS.open(filePath.c_str(), FILE_O_READ);
      if (f) {
          f.read(g_slots[i].aes_key, 16);
          f.read((uint8_t*)&g_slots[i].counter, 4);
          f.read((uint8_t*)g_slots[i].name, sizeof(g_slots[i].name));
          f.close();
      }
  }

  // Fallback to legacy prov logic for Slot 1 if /slot1.dat didn't exist
  if (key_is_all_zeros(g_slots[1].aes_key)) {
      immo::prov_load_key_or_zero(immo::DEFAULT_PROV_PATH, g_slots[1].aes_key); 
  }
  
  // NOTE: Assuming PIN is also stored or using a default. For simplicity, reading PIN from flash if possible,
  // but if SETPIN serial command is only way to set it, we might need to store it.
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

void handle_valid_command(const immo::Payload& pl, uint8_t slot_id) {
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
    case immo::Command::Window:
      if (slot_id == 0) { // Only Uguisu can open the window
          g_window_active = true;
          g_window_end_ms = millis() + WINDOW_DURATION_MS;
          g_window_remaining_ms = WINDOW_DURATION_MS;
          // Three fast beeps at 4 kHz
          for (int i = 0; i < 3; i++) {
              buzzer_tone_ms(4000, 100);
              delay(50);
          }
          state_changed = true;
      }
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
  
  immo::Payload pl{};
  uint8_t slot_id;
  if (!immo::verify_payload(ct, mic, g_slots, pl, slot_id)) return;
  handle_valid_command(pl, slot_id);
}

void unlock_lock_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    if (len != immo::PAYLOAD_LEN) return;
    uint8_t ct[immo::MSG_LEN];
    uint8_t mic[immo::MIC_LEN];
    memcpy(ct, data, immo::MSG_LEN);
    memcpy(mic, data + immo::MSG_LEN, immo::MIC_LEN);
    
    immo::Payload pl{};
    uint8_t slot_id;
    if (!immo::verify_payload(ct, mic, g_slots, pl, slot_id)) return;
    handle_valid_command(pl, slot_id);
}

void send_json_response(const JsonDocument& doc) {
    char buf[247];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (Bluefruit.connected() && mgmtRspChr.notifyEnabled()) {
        mgmtRspChr.notify(buf, len);
    }
    Serial.println(buf);
}

void process_management_command(String cmd, bool is_serial, bool is_authenticated) {
    StaticJsonDocument<512> rsp;
    rsp["status"] = "error";

    // Split command by ':'
    int max_parts = 6;
    String parts[max_parts];
    int part_count = 0;
    int start_idx = 0;
    while (start_idx < (int)cmd.length() && part_count < max_parts) {
        int colon_idx = cmd.indexOf(':', start_idx);
        if (colon_idx == -1) {
            parts[part_count++] = cmd.substring(start_idx);
            break;
        } else {
            parts[part_count++] = cmd.substring(start_idx, colon_idx);
            start_idx = colon_idx + 1;
        }
    }

    String action = parts[0];

    if (action == "SETPIN") {
        if (!is_serial && !(g_window_active && slot_1_is_empty())) {
            rsp["reason"] = "serial_or_window_only";
        } else if (part_count >= 2) {
            String pinStr = parts[1];
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
        } else {
             rsp["reason"] = "missing_args";
        }
    } else if (action == "RESETLOCK") {
        if (!is_serial && !(g_window_active && slot_1_is_empty())) {
            rsp["reason"] = "serial_or_window_only";
        } else {
            InternalFS.format();
            rsp["status"] = "ok";
            send_json_response(rsp);
            delay(100);
            NVIC_SystemReset();
        }
    } else if (action == "PROV") {
        if (!is_serial && g_session_slot != 1 && !(g_window_active && slot_1_is_empty() && parts[1].toInt() == 1)) {
            rsp["reason"] = "forbidden";
        } else if (part_count >= 4) {
            int slot = parts[1].toInt();
            if (slot >= 0 && slot < immo::MAX_KEY_SLOTS) {
                // Parse hex key
                String keyStr = parts[2];
                if (keyStr.length() == 32) {
                     for (int i=0; i<16; i++) {
                         g_slots[slot].aes_key[i] = strtol(keyStr.substring(i*2, i*2+2).c_str(), NULL, 16);
                     }
                     g_slots[slot].counter = parts[3].toInt();
                     if (part_count >= 5) {
                         strncpy(g_slots[slot].name, parts[4].c_str(), sizeof(g_slots[slot].name) - 1);
                     } else {
                         g_slots[slot].name[0] = '\0';
                     }
                     
                     // In reality, save to LittleFS here (e.g. "/slotX.dat")
                     String filePath = String("/slot") + slot + ".dat";
                     File f = InternalFS.open(filePath.c_str(), FILE_O_WRITE);
                     if (f) {
                         f.write(g_slots[slot].aes_key, 16);
                         f.write((uint8_t*)&g_slots[slot].counter, 4);
                         f.write(g_slots[slot].name, sizeof(g_slots[slot].name));
                         f.close();
                     }

                     rsp["status"] = "ok";
                     rsp["slot"] = slot;
                     rsp["name"] = g_slots[slot].name;
                     rsp["counter"] = g_slots[slot].counter;
                } else {
                     rsp["reason"] = "invalid_key_length";
                }
            } else {
                rsp["reason"] = "invalid_slot";
            }
        } else {
             rsp["reason"] = "missing_args";
        }
    } else if (action == "RENAME") {
        if (!is_serial && g_session_slot != 1) {
            rsp["reason"] = "forbidden";
        } else if (part_count >= 3) {
            int slot = parts[1].toInt();
            if (slot >= 0 && slot < immo::MAX_KEY_SLOTS) {
                strncpy(g_slots[slot].name, parts[2].c_str(), sizeof(g_slots[slot].name) - 1);
                // Save to LittleFS
                String filePath = String("/slot") + slot + ".dat";
                File f = InternalFS.open(filePath.c_str(), FILE_O_WRITE);
                if (f) {
                    f.write(g_slots[slot].aes_key, 16);
                    f.write((uint8_t*)&g_slots[slot].counter, 4);
                    f.write(g_slots[slot].name, sizeof(g_slots[slot].name));
                    f.close();
                }
                rsp["status"] = "ok";
                rsp["slot"] = slot;
                rsp["name"] = g_slots[slot].name;
            } else {
                rsp["reason"] = "invalid_slot";
            }
        } else {
            rsp["reason"] = "missing_args";
        }
    } else if (action == "SLOTS?") {
        rsp["status"] = "ok";
        JsonArray slots = rsp.createNestedArray("slots");
        for (int i=0; i<immo::MAX_KEY_SLOTS; i++) {
             JsonObject slotObj = slots.createNestedObject();
             slotObj["id"] = i;
             slotObj["used"] = !key_is_all_zeros(g_slots[i].aes_key);
             slotObj["counter"] = g_slots[i].counter;
             slotObj["name"] = g_slots[i].name;
        }
    } else if (action == "REVOKE") {
        if (!is_serial && g_session_slot != 1) {
            rsp["reason"] = "forbidden";
        } else if (part_count >= 2) {
            int slot = parts[1].toInt();
            if (slot >= 0 && slot < immo::MAX_KEY_SLOTS) {
                memset(g_slots[slot].aes_key, 0, 16);
                g_slots[slot].counter = 0;
                g_slots[slot].name[0] = '\0';
                String filePath = String("/slot") + slot + ".dat";
                InternalFS.remove(filePath.c_str());
                rsp["status"] = "ok";
                rsp["slot"] = slot;
            } else {
                rsp["reason"] = "invalid_slot";
            }
        } else {
             rsp["reason"] = "missing_args";
        }
    } else if (action == "RECOVER") {
        if (!is_serial && (!g_window_active || g_locked)) {
             rsp["reason"] = "forbidden_no_window_or_locked";
        } else if (part_count >= 4) {
            int slot = parts[1].toInt();
            if (slot == 1 && !is_serial && !is_authenticated) {
                 // RECOVER:1 requires SMP authenticated link if not serial
                 rsp["reason"] = "forbidden_needs_auth";
            } else if (slot >= 0 && slot < immo::MAX_KEY_SLOTS) {
                String keyStr = parts[2];
                if (keyStr.length() == 32) {
                     for (int i=0; i<16; i++) {
                         g_slots[slot].aes_key[i] = strtol(keyStr.substring(i*2, i*2+2).c_str(), NULL, 16);
                     }
                     g_slots[slot].counter = parts[3].toInt();
                     if (part_count >= 5) {
                         strncpy(g_slots[slot].name, parts[4].c_str(), sizeof(g_slots[slot].name) - 1);
                     } else {
                         g_slots[slot].name[0] = '\0';
                     }
                     
                     String filePath = String("/slot") + slot + ".dat";
                     File f = InternalFS.open(filePath.c_str(), FILE_O_WRITE);
                     if (f) {
                         f.write(g_slots[slot].aes_key, 16);
                         f.write((uint8_t*)&g_slots[slot].counter, 4);
                         f.write(g_slots[slot].name, sizeof(g_slots[slot].name));
                         f.close();
                     }

                     rsp["status"] = "ok";
                     rsp["slot"] = slot;
                     rsp["name"] = g_slots[slot].name;
                     rsp["counter"] = g_slots[slot].counter;
                } else {
                     rsp["reason"] = "invalid_key_length";
                }
            } else {
                rsp["reason"] = "invalid_slot";
            }
        } else {
             rsp["reason"] = "missing_args";
        }
    } else if (action == "IDENTIFY") {
         rsp["reason"] = "use_binary_payload";
    } else {
        rsp["reason"] = "unknown_command";
    }

    send_json_response(rsp);
}

void mgmt_cmd_write_callback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    // If it's 14 bytes exactly, it might be an IDENTIFY binary payload
    if (len == immo::PAYLOAD_LEN) {
        uint8_t ct[immo::MSG_LEN];
        uint8_t mic[immo::MIC_LEN];
        memcpy(ct, data, immo::MSG_LEN);
        memcpy(mic, data + immo::MSG_LEN, immo::MIC_LEN);
        
        immo::Payload pl{};
        uint8_t slot_id;
        if (immo::verify_payload(ct, mic, g_slots, pl, slot_id)) {
             if (pl.command == immo::Command::Identify) {
                  const uint32_t last = g_store.lastCounter();
                  if (pl.counter > last) {
                      g_store.update(pl.counter);
                      g_session_slot = slot_id;
                      StaticJsonDocument<256> rsp;
                      rsp["status"] = "ok";
                      rsp["action"] = "IDENTIFY";
                      send_json_response(rsp);
                  }
             }
        }
        return; // Handled as binary
    }

    char buf[248];
    uint16_t copy_len = len < 247 ? len : 247;
    memcpy(buf, data, copy_len);
    buf[copy_len] = '\0';
    String cmd(buf);

    // Some commands don't need IDENTIFY
    if (cmd == "SLOTS?" || cmd.startsWith("RECOVER:")) {
        // Allow
    } else if (g_session_slot == -1) {
        // Must be IDENTIFY'd first for other commands
        StaticJsonDocument<256> rsp;
        rsp["status"] = "error";
        rsp["reason"] = "unidentified";
        send_json_response(rsp);
        return; 
    }
    
    // Check if the connection is authenticated (has SMP bonding/PIN)
    bool is_auth = false;
    ble_gap_conn_sec_t sec;
    if (sd_ble_gap_conn_sec_get(conn_hdl, &sec) == NRF_SUCCESS) {
        is_auth = (sec.sec_mode.sm >= 1 && sec.sec_mode.lv >= 2);
    }
    
    process_management_command(cmd, false, is_auth);
}

void prph_connect_callback(uint16_t conn_handle) {
    g_session_slot = -1; // Reset identity on connection
    if (g_window_active) {
        if (millis() < g_window_end_ms) {
            g_window_remaining_ms = g_window_end_ms - millis();
        } else {
            g_window_remaining_ms = 0;
            g_window_active = false;
        }
    }
}

void prph_disconnect_callback(uint16_t conn_handle, uint8_t reason) {
    g_session_slot = -1;
    if (g_window_active) {
        g_window_end_ms = millis() + g_window_remaining_ms;
    }
}

void update_advertising() {
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();
    
    Bluefruit.Advertising.setBeacon(iBeacon);

    // Scan Response packet for Proximity UUID
    if (g_window_active) {
        Bluefruit.ScanResponse.addUuid(bleUuidWindow);
        Bluefruit.Advertising.setInterval(160, 160); // 100 ms (160 * 0.625)
    } else if (g_locked) {
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
  immo::ensure_provisioned(immo::DEFAULT_PROV_TIMEOUT_MS, on_provision_success, load_psk_from_storage, slot_1_is_empty);

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
  // We use SECMODE_OPEN because the Provisioning Window and RECOVER commands need to be accessed
  // WITHOUT pairing. We enforce authentication manually in mgmt_cmd_write_callback for commands that need it.
  mgmtCmdChr.setPermission(SECMODE_OPEN, SECMODE_OPEN); 
  mgmtCmdChr.setWriteCallback(mgmt_cmd_write_callback);
  mgmtCmdChr.begin();

  // Setup Management Response Characteristic (Notify)
  mgmtRspChr.setProperties(CHR_PROPS_NOTIFY);
  mgmtRspChr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
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
  
  if (g_window_active && !Bluefruit.connected()) {
      if (millis() >= g_window_end_ms) {
          g_window_active = false;
          update_advertising();
      }
  }

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
