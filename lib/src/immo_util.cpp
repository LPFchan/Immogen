#include "immo_util.h"
#include <Arduino.h>

namespace immo {

bool is_key_blank(const uint8_t key[16]) {
  for (int i = 0; i < 16; i++)
    if (key[i] != 0) return false;
  return true;
}

[[noreturn]] void led_error_loop(int led_pin) {
  if (led_pin >= 0) {
    pinMode(led_pin, OUTPUT);
    while (true) {
      digitalWrite(led_pin, HIGH);
      delay(200);
      digitalWrite(led_pin, LOW);
      delay(200);
    }
  }
  while (true) delay(1000);
}

}  // namespace immo
