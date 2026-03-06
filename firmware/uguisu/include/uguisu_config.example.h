#pragma once

// BLE Manufacturer Specific Data "company id" (2 bytes, little-endian).
// 0xFFFF is a common placeholder for prototypes.
#define UGUISU_COMPANY_ID 0xFFFF

// Button pin used to trigger a one-shot advertisement burst.
// Pick a real GPIO used in your Uguisu hardware.
#define UGUISU_PIN_BUTTON D1

// nRF52840 P0.x for wake-from-system-off (must match UGUISU_PIN_BUTTON; e.g. XIAO D1 = P0.03).
#define UGUISU_PIN_BUTTON_NRF 3

// How long to advertise after a press (ms).
#define UGUISU_ADVERTISE_MS 2000

// How often to send advertisement packets while active (ms).
#define UGUISU_ADV_INTERVAL_MS 100

// Long-press threshold for lock command (ms). Press >= this = Lock, else Unlock.
#define UGUISU_LONG_PRESS_MS 1000

