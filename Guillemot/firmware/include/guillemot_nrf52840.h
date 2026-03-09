#pragma once
#include <Arduino.h>

// GPIO (matches the design document)
static constexpr int PIN_LATCH_SET = D0;
static constexpr int PIN_LATCH_RESET = D1;
static constexpr int PIN_BUZZER = D3;

// BLE scanning duty cycle
static constexpr uint16_t SCAN_INTERVAL_MS = 500;
static constexpr uint16_t SCAN_WINDOW_MS = 25;

// Manufacturer Specific Data company ID (2 bytes, little-endian in adverts).
// Set to your chosen value; must match the fob when you use one. See PROTOCOL.md.
static constexpr uint16_t MSD_COMPANY_ID = 0xFFFF;

// Buzzer settings — tune with tools/buzzer_tuner.html
#define BUZZER_LOW_HZ   2637
#define BUZZER_HIGH_HZ  3952
#define BUZZER_LOW_MS   130
#define BUZZER_HIGH_MS  130

// Latch pulse widths
static constexpr uint16_t LATCH_PULSE_MS = 15;

// Error indicator: onboard LED for blink loop when InternalFS fails.
// XIAO nRF52840 red LED is P0.26 (D2); set -1 if no LED.
#ifndef PIN_ERROR_LED
#define PIN_ERROR_LED 26
#endif

inline void latch_set_pulse() {
  digitalWrite(PIN_LATCH_RESET, LOW);
  digitalWrite(PIN_LATCH_SET, HIGH);
  delay(LATCH_PULSE_MS);
  digitalWrite(PIN_LATCH_SET, LOW);
}

inline void latch_reset_pulse() {
  digitalWrite(PIN_LATCH_SET, LOW);
  digitalWrite(PIN_LATCH_RESET, HIGH);
  delay(LATCH_PULSE_MS);
  digitalWrite(PIN_LATCH_RESET, LOW);
}
