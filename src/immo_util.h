#pragma once
#include <stdint.h>

namespace immo {

// Returns true if all 16 bytes of a PSK are zero (not provisioned).
bool is_key_blank(const uint8_t key[16]);

// Fatal error indicator: blinks the given LED pin forever.
// Pass -1 (or any negative value) if no LED is available (spins idle).
[[noreturn]] void led_error_loop(int led_pin);

}  // namespace immo
