#pragma once
#include <stdint.h>
#include <stddef.h>

namespace immo {

// Returns true if VBUS is present.
bool prov_is_vbus_present();

// Wait for a valid PROV: string on Serial. Times out after `timeout_ms`.
// If valid, calls `on_success` with the parsed device ID, 16-byte key, and counter.
// `on_success` should return true if it successfully wrote the key to storage.
bool prov_run_serial_loop(uint32_t timeout_ms, bool (*on_success)(uint16_t device_id, const uint8_t key[16], uint32_t counter));

}  // namespace immo
