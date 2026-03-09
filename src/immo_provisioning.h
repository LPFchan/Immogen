#pragma once
#include <stdint.h>
#include <stddef.h>
#include "immo_storage.h"

namespace immo {

// Magic prefix written before the PSK in flash. "PROV" in memory on LE ARM.
static constexpr uint32_t PROV_MAGIC = 0x564F5250u;

// Write key to flash with PROV_MAGIC prefix, verify readback, seed counter store,
// and copy key to runtime buffer. Returns false if write or verification fails.
bool prov_write_and_verify(
    const char* path,
    const uint8_t key[16],
    uint32_t counter,
    CounterStore& store,
    uint8_t* runtime_key
);

// Load key from flash. Returns true if file has valid PROV_MAGIC + 16-byte key.
bool prov_load_key(const char* path, uint8_t out_key[16]);

// Returns true if VBUS is present.
bool prov_is_vbus_present();

// Wait for a valid PROV: string on Serial. Times out after `timeout_ms`.
// Format: PROV:<key_32_hex>:<counter_8_hex>:<checksum_4_hex>
// If valid, calls `on_success` with the parsed 16-byte key and counter.
// `on_success` should return true if it successfully wrote the key to storage.
bool prov_run_serial_loop(uint32_t timeout_ms, bool (*on_success)(const uint8_t key[16], uint32_t counter));

// Runs the provisioning loop if VBUS is present.
// If VBUS is present, it will run the loop once for `timeout_ms` to allow re-provisioning.
// If `is_provisioned` returns false after that, and VBUS is still present,
// it will loop indefinitely until provisioning succeeds or VBUS is disconnected.
void ensure_provisioned(
    uint32_t timeout_ms,
    bool (*on_success)(const uint8_t[16], uint32_t),
    void (*load_provisioning)(),
    bool (*is_provisioned)()
);

}  // namespace immo
