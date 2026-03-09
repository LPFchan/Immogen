#pragma once
#include <Arduino.h>

// LED effects for Uguisu RGB LED (XL-5050RGBC, active-low: 0=on, 255=off).
// Pins defined in uguisu_config.h: PIN_LED_R=D2, PIN_LED_G=D3, PIN_LED_B=D1.
//
// Timing constants — tune with tools/led_visualizer.html, then paste here.

// --- Unlock / Lock single-flash ---
#ifndef LED_FLASH_RISE_MS
#define LED_FLASH_RISE_MS   300
#endif
#ifndef LED_FLASH_HOLD_MS
#define LED_FLASH_HOLD_MS   150
#endif
#ifndef LED_FLASH_FALL_MS
#define LED_FLASH_FALL_MS   300
#endif

// --- Serial provisioning pulsed blue blink ---
#ifndef LED_PROV_RISE_MS
#define LED_PROV_RISE_MS   500
#endif
#ifndef LED_PROV_HOLD_MS
#define LED_PROV_HOLD_MS   200
#endif
#ifndef LED_PROV_FALL_MS
#define LED_PROV_FALL_MS   500
#endif
#ifndef LED_PROV_OFF_MS
#define LED_PROV_OFF_MS    50
#endif

// --- Low-battery pulsed red flash ---
#ifndef LED_LOWBAT_RISE_MS
#define LED_LOWBAT_RISE_MS    120
#endif
#ifndef LED_LOWBAT_HOLD_MS
#define LED_LOWBAT_HOLD_MS    100
#endif
#ifndef LED_LOWBAT_FALL_MS
#define LED_LOWBAT_FALL_MS    120
#endif
#ifndef LED_LOWBAT_OFF_MS
#define LED_LOWBAT_OFF_MS     170
#endif
#ifndef LED_LOWBAT_WINDOW_MS
#define LED_LOWBAT_WINDOW_MS  2000
#endif

// Low-battery threshold in millivolts (tune for your cell chemistry).
#ifndef LED_LOWBAT_MV_THRESHOLD
#define LED_LOWBAT_MV_THRESHOLD  3400
#endif

namespace led {

// Active-low: invert brightness for the physical pin.
static inline void _write(uint8_t pin, uint8_t brightness) {
  analogWrite(pin, 255 - brightness);
}

// Initialize all RGB LED pins as output, LEDs off.
inline void init() {
  pinMode(PIN_LED_R, OUTPUT); _write(PIN_LED_R, 0);
  pinMode(PIN_LED_G, OUTPUT); _write(PIN_LED_G, 0);
  pinMode(PIN_LED_B, OUTPUT); _write(PIN_LED_B, 0);
}

// Turn all LEDs off.
inline void off() {
  _write(PIN_LED_R, 0);
  _write(PIN_LED_G, 0);
  _write(PIN_LED_B, 0);
}

// Blocking fade-in on one pin (off → full) over duration_ms.
inline void fade_in(uint8_t pin, uint32_t duration_ms) {
  const uint32_t steps = 64;
  const uint32_t step_ms = duration_ms / steps;
  for (uint32_t i = 0; i <= steps; i++) {
    _write(pin, (uint8_t)(i * 255 / steps));
    delay(step_ms);
  }
}

// Blocking fade-out on one pin (full → off) over duration_ms.
inline void fade_out(uint8_t pin, uint32_t duration_ms) {
  const uint32_t steps = 64;
  const uint32_t step_ms = duration_ms / steps;
  for (uint32_t i = 0; i <= steps; i++) {
    _write(pin, 255 - (uint8_t)(i * 255 / steps));
    delay(step_ms);
  }
}

// Blocking single flash: fade-in → hold → fade-out.
inline void flash_once(uint8_t pin, uint32_t rise_ms, uint32_t hold_ms, uint32_t fall_ms) {
  fade_in(pin, rise_ms);
  delay(hold_ms);
  fade_out(pin, fall_ms);
  _write(pin, 0);
}

// One pulsed blink cycle: fade-in → hold → fade-out → silent gap.
// Call in a loop from a FreeRTOS task for non-blocking use (e.g. DFU).
inline void pulse_once(uint8_t pin,
                       uint32_t rise_ms, uint32_t hold_ms,
                       uint32_t fall_ms, uint32_t off_ms) {
  fade_in(pin, rise_ms);
  delay(hold_ms);
  fade_out(pin, fall_ms);
  _write(pin, 0);
  delay(off_ms);
}

// Blocking pulsed blink loop for total_ms (use for low-battery warning).
inline void pulse_blink(uint8_t pin,
                        uint32_t rise_ms, uint32_t hold_ms,
                        uint32_t fall_ms, uint32_t off_ms,
                        uint32_t total_ms) {
  const uint32_t deadline = millis() + total_ms;
  while (millis() < deadline) {
    if (millis() < deadline) fade_in(pin, rise_ms);
    if (millis() < deadline) delay(hold_ms);
    if (millis() < deadline) fade_out(pin, fall_ms);
    _write(pin, 0);
    if (millis() < deadline) delay(off_ms);
  }
  _write(pin, 0);
}

// Fatal error: rapid red blink on RGB + onboard LED forever.
// Self-contained — safe to call before init().
[[noreturn]] inline void error_loop(int onboard_pin = PIN_ERROR_LED) {
  pinMode(PIN_LED_R, OUTPUT);
  if (onboard_pin >= 0) pinMode(onboard_pin, OUTPUT);
  while (true) {
    _write(PIN_LED_R, 255);
    if (onboard_pin >= 0) digitalWrite(onboard_pin, HIGH);
    delay(150);
    _write(PIN_LED_R, 0);
    if (onboard_pin >= 0) digitalWrite(onboard_pin, LOW);
    delay(150);
  }
}

// Convenience wrappers using the configured defaults.
inline void flash_unlock() {
  flash_once(PIN_LED_G, LED_FLASH_RISE_MS, LED_FLASH_HOLD_MS, LED_FLASH_FALL_MS);
}
inline void flash_lock() {
  flash_once(PIN_LED_R, LED_FLASH_RISE_MS, LED_FLASH_HOLD_MS, LED_FLASH_FALL_MS);
}
inline void flash_low_battery(uint8_t pin = PIN_LED_R) {
  pulse_blink(pin,
              LED_LOWBAT_RISE_MS, LED_LOWBAT_HOLD_MS,
              LED_LOWBAT_FALL_MS, LED_LOWBAT_OFF_MS,
              LED_LOWBAT_WINDOW_MS);
}

// Provisioning task body — call in an infinite loop from a FreeRTOS task.
// Example in main.cpp:
//   static void prov_led_task(void*) {
//     while (true) led::prov_pulse();
//   }
inline void prov_pulse() {
  pulse_once(PIN_LED_B,
             LED_PROV_RISE_MS, LED_PROV_HOLD_MS,
             LED_PROV_FALL_MS, LED_PROV_OFF_MS);
}

}  // namespace led
