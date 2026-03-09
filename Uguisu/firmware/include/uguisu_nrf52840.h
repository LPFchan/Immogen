#pragma once
#include <Arduino.h>
#include <nrf_gpio.h>
#include <nrf_soc.h>
#include <bluefruit.h>

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
#define UGUISU_ADVERTISE_MS 600

// How often to send advertisement packets while active (ms).
// Minimum achievable with nRF52840 SoftDevice S140 legacy advertising: 20 ms.
#define UGUISU_ADV_INTERVAL_MS 20

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
#ifdef VBAT_ENABLE
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);  // enable PMOS → connect 1MΩ/1MΩ divider
#endif
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(1);  // allow divider to settle
  const uint16_t mv = (uint16_t)((uint32_t)analogRead(UGUISU_VBAT_PIN) * 6000u / 4096u);
#ifdef VBAT_ENABLE
  digitalWrite(VBAT_ENABLE, HIGH);  // disable PMOS → stop divider current draw
#endif
  return mv;
}

// Stop BLE advertising, configure button as wake source, enter nRF52 system-off.
// Safe to call before advertising has started (stop() is a no-op in that case).
inline void system_off() {
  Bluefruit.Advertising.stop();
  delay(10);
#ifdef UGUISU_PIN_BUTTON_NRF
  nrf_gpio_cfg_sense_input(UGUISU_PIN_BUTTON_NRF, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
#endif
  sd_power_system_off();
}
