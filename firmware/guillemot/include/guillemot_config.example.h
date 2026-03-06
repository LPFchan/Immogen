#pragma once

// GPIO (matches the design document)
static constexpr int PIN_LATCH_SET = D0;
static constexpr int PIN_LATCH_RESET = D1;
static constexpr int PIN_BUZZER = D3;

// BLE scanning duty cycle
static constexpr uint16_t SCAN_INTERVAL_MS = 2000;
static constexpr uint16_t SCAN_WINDOW_MS = 20;

// Manufacturer Specific Data company ID used to filter adverts.
// The advertising MSD payload format is: [company_id_le(2)] + [11-byte protocol payload]
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

