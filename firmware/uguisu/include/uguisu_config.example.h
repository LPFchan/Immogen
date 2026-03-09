#pragma once

// BLE Manufacturer Specific Data company ID (2 bytes, little-endian).
// Must match Guillemot. 0xFFFF is a common placeholder for prototypes.
static constexpr uint16_t MSD_COMPANY_ID = 0xFFFF;

// Button pin used to trigger a one-shot advertisement burst.
#define UGUISU_PIN_BUTTON D0

// nRF52840 P0.x for wake-from-system-off (must match UGUISU_PIN_BUTTON; XIAO D0 = P0.02).
#define UGUISU_PIN_BUTTON_NRF 2

// RGB LED (LED1, XL-5050RGBC): B=D1, R=D2, G=D3. Active-low (sink to turn on).
#define PIN_LED_B D1
#define PIN_LED_R D2
#define PIN_LED_G D3

// How long to advertise after a press (ms).
#define UGUISU_ADVERTISE_MS 2000

// How often to send advertisement packets while active (ms).
#define UGUISU_ADV_INTERVAL_MS 100

// Long-press threshold for lock command (ms). Press >= this = Lock, else Unlock.
#define UGUISU_LONG_PRESS_MS 1000

// Max time to wait for button press before sleeping (ms).
#define UGUISU_BUTTON_TIMEOUT_MS 10000

// Error indicator: onboard LED for blink loop when InternalFS fails. Set -1 if no LED.
#ifndef PIN_ERROR_LED
#define PIN_ERROR_LED 26
#endif

// VBAT ADC: P0.31 (AIN7) through 1MΩ/1MΩ voltage divider on XIAO nRF52840.
// Arduino pin 32 is the last entry in g_ADigitalPinMap and maps to P0.31.
// VBAT_mV = raw_12bit * 3000mV_ref * 2 (divider comp) / 4096 ≈ 1.464 mV/LSB.
#ifndef UGUISU_VBAT_PIN
#  ifdef PIN_VBAT
#    define UGUISU_VBAT_PIN  PIN_VBAT
#  else
#    define UGUISU_VBAT_PIN  32  // P0.31 on XIAO nRF52840
#  endif
#endif

inline uint16_t readVbat_mv() {
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  return (uint16_t)((uint32_t)analogRead(UGUISU_VBAT_PIN) * 6000u / 4096u);
}

