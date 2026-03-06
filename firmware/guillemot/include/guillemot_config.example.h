#pragma once

// GPIO (matches the design document)
static constexpr int PIN_LATCH_SET = D0;
static constexpr int PIN_LATCH_RESET = D1;
static constexpr int PIN_BUZZER = D3;

// BLE scanning duty cycle
static constexpr uint16_t SCAN_INTERVAL_MS = 2000;
static constexpr uint16_t SCAN_WINDOW_MS = 20;

// Manufacturer Specific Data company ID (2 bytes, little-endian in adverts).
// Set to your chosen value; must match the fob when you use one. See PROTOCOL.md.
static constexpr uint16_t MSD_COMPANY_ID = 0xFFFF;

// Protocol sizes
static constexpr size_t PAYLOAD_LEN = 11;
static constexpr size_t MIC_LEN = 4;

// Buzzer settings
static constexpr uint16_t BUZZER_HZ = 4000;
static constexpr uint16_t BUZZER_UNLOCK_MS = 120;
static constexpr uint16_t BUZZER_LOCK_MS = 200;

// Latch pulse widths
static constexpr uint16_t LATCH_PULSE_MS = 15;

// Error indicator: onboard LED for blink loop when InternalFS fails.
// XIAO nRF52840 red LED is P0.26 (D2); set -1 if no LED.
#ifndef PIN_ERROR_LED
#define PIN_ERROR_LED 26
#endif

