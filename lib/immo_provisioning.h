#pragma once
#include <stdint.h>
#include <stddef.h>
#include "immo_storage.h"

namespace immo {

// Magic prefix written before the PSK in flash. "PROV" in memory on LE ARM.
static constexpr uint32_t PROV_MAGIC = 0x564F5250u;

static constexpr const char* DEFAULT_PROV_PATH = "/prov.bin";
static constexpr uint32_t DEFAULT_PROV_TIMEOUT_MS = 30000;

// Write key to flash with PROV_MAGIC prefix, verify readback, seed counter store,
// and copy key to runtime buffer. Returns false if write or verification fails.
bool prov_write_and_verify(
    const char* path,
    uint8_t slot_id,
    const uint8_t key[16],
    uint32_t counter,
    const char* name,
    CounterStore& store,
    KeySlot slots[MAX_KEY_SLOTS]
);

// Load key from flash. Returns true if file has valid PROV_MAGIC + 16-byte key.
bool prov_load_key(const char* path, uint8_t& out_slot_id, uint8_t out_key[16], char out_name[24]);

// Load key from flash, zeroing out_key if not found or invalid.
void prov_load_key_or_zero(const char* path, uint8_t& out_slot_id, uint8_t out_key[16], char out_name[24]);

// Returns true if VBUS is present.
bool prov_is_vbus_present();

// Wait for a valid PROV: string on Serial. Times out after `timeout_ms`.
// Format: PROV:<slot_1_hex>:<key_32_hex>:<counter_8_hex>:<name_url_encoded>
// If valid, calls `on_success` with the parsed slot_id, 16-byte key, counter, and name.
// `on_success` should return true if it successfully wrote the key to storage.
bool prov_run_serial_loop(uint32_t timeout_ms, bool (*on_success)(uint8_t slot_id, const uint8_t key[16], uint32_t counter, const char* name));

// Runs the provisioning loop if VBUS is present.
// If VBUS is present, it will run the loop once for `timeout_ms` to allow re-provisioning.
// If `is_provisioned` returns false after that, and VBUS is still present,
// it will loop indefinitely until provisioning succeeds or VBUS is disconnected.
void ensure_provisioned(
    uint32_t timeout_ms,
    bool (*on_success)(uint8_t, const uint8_t[16], uint32_t, const char*),
    void (*load_provisioning)(),
    bool (*is_unprovisioned)()
);

}  // namespace immo
