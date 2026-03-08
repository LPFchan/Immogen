# Guillemot Codebase Architecture & Technical Documentation

Guillemot is the deck receiver unit of a three-part immobilizer system designed for the Ninebot Max G30 (alongside the Uguisu fob and Whimbrel web app). It validates encrypted Bluetooth Low Energy (BLE) advertisements and manipulates an SR latch to gate battery-to-ESC power.

This document serves as a comprehensive technical writeup of the Guillemot hardware and firmware codebase.

---

## 1. Hardware Architecture

The hardware is designed as a fully reversible inline splice via XT60 connectors, residing inside the scooter's deck. It requires zero standby software processing once latched, achieving an ultra-low standby current of ~22 µA.

### Core Components
*   **Microcontroller (U1):** Seeed Studio XIAO nRF52840. Handles BLE scanning, cryptographic validation, latch pulsing, and PWM buzzer control.
*   **SR Latch (U2):** SN74LVC2G02DCTR (dual NOR gate wired as an SR latch). Maintains the system's lock/unlock state even if the MCU sleeps or crashes. 
    *   `Q` (Output) HIGH = Unlocked.
    *   A Power-On-Reset (POR) differentiator circuit (10nF capacitor + 100kΩ resistor) ensures the latch boots in a RESET (Locked) state.
*   **Power Control (MOSFETs):**
    *   **Main FET (Q1):** IPB042N10N3G (100V / 137A). Handles the primary return path (low-side switching).
    *   **Pre-charge FET (Q2):** AO3422. Activated by the latch to softly pre-charge the ESC capacitors, limiting inrush current.
    *   **Bleeder P-FET (Q3):** SI2309CDS. Disconnects the 10kΩ bleeder resistor when locked to achieve 0 µA bleed.
*   **Buzzer (BZ1):** Piezo buzzer driven by MMBT3904 BJTs, separated from the gate drive via isolation diodes (1N4148W) to prevent voltage sag on the latch/MOSFET gate during tones.
*   **Buck Converter (PS1):** TPSM365R6V3RDNR. Efficiently drops the scooter's raw battery voltage (up to 42V) down to 3.3V for the XIAO with an ultra-low quiescent current (4 µA).

---

## 2. Firmware Architecture

The firmware is built using PlatformIO, leveraging the Adafruit nRF52 Arduino Core.

### Core Logic (`src/main.cpp`)
The main firmware application focuses on setup, BLE scanning, and hardware interactions.

*   `setup()`: Initializes GPIO (latch pins, buzzer), sets the pins to safe default states, and mounts the LittleFS filesystem for reading the AES Pre-Shared Key (PSK) and counters. It invokes the provisioning loop and begins BLE scanning.
*   `loop()`: Invokes `sd_app_evt_wait()`, putting the CPU to sleep to save power. Operations are entirely interrupt/callback driven.
*   **BLE Scanning (`scan_callback`):**
    *   Triggers every time a BLE advertisement is captured.
    *   Calls `parse_payload_from_report()` to extract Manufacturer Specific Data matching the `MSD_COMPANY_ID` (0xFFFF by default).
    *   The payload (13 bytes) consists of a 4-byte monotonic counter, a 1-byte command (Lock `0x02` or Unlock `0x01`), and an 8-byte AES-CCM MIC.
*   **Validation & Execution:**
    *   `verify_payload()`: Reconstructs the expected message and nonce, calculating the AES-CCM MIC. If the calculated MIC matches the received MIC in constant time, the payload is authentic.
    *   `handle_valid_command()`: Rejects payloads with a counter less than or equal to the last stored counter (anti-replay). If valid, it pulses the latch (`latch_set_pulse` or `latch_reset_pulse`), sounds the buzzer, and records the new counter to flash.

### Configuration (`include/guillemot_config.h`)
Stores compile-time constants:
*   **Hardware Mapping:** `PIN_LATCH_SET` (D0), `PIN_LATCH_RESET` (D1), `PIN_BUZZER` (D3).
*   **RF Timing:** `SCAN_INTERVAL_MS` (2000 ms) and `SCAN_WINDOW_MS` (20 ms). A 1% duty cycle heavily conserves battery.
*   **Buzzer Settings:** Frequencies (`4000 Hz`) and tone durations for locking and unlocking.

---

## 3. ImmoCommon Library

Stored in `lib/ImmoCommon`, this submodule is shared between the Guillemot receiver and the Uguisu fob. It contains the cryptographic primitives, flash storage management, and serial provisioning flow.

### Cryptography (`immo_crypto.cpp` / `.h`)
Implements AES-128 CCM (Counter with CBC-MAC) for payload authentication without requiring heavy cryptographic libraries.
*   `build_nonce()` & `build_msg()`: Structures the data correctly for AES-CCM.
*   `ccm_mic_8()`: Calculates an 8-byte Message Integrity Code (MIC) over the payload using hardware ECB encryption (`sd_ecb_block_encrypt`) provided by the Nordic SoftDevice.
*   `constant_time_eq()`: Compares the received MIC with the expected MIC using bitwise XOR to prevent timing side-channel attacks.

### Flash Storage (`immo_storage.cpp` / `.h`)
Handles persistent storage using `InternalFileSystem` (LittleFS). Used heavily for the anti-replay monotonic counter.
*   `CounterStore`: A class that appends counter records sequentially (`CounterRecord`: 4-byte counter + 4-byte CRC32) to a log file (`/ctr.log`).
*   **Wear-leveling:** By appending small records rather than constantly erasing and rewriting a single flash page, flash wear is minimized. Once `max_bytes_` (4096 bytes) is reached, `rotateIfNeeded_()` rotates the log to `/ctr.old`.

### Provisioning (`immo_provisioning.cpp` / `.h`)
Handles the initial secure onboarding of the device using the Whimbrel web app.
*   `prov_is_vbus_present()`: Detects if a USB cable is physically connected.
*   `prov_run_serial_loop()`: Listens over the serial port for a specific `PROV:` payload containing a 16-byte hex AES key, a starting counter, and a CRC16 checksum.
*   `ensure_provisioned()`: During boot, if USB is connected and the key is unset (all zeros), it traps the MCU in a provisioning loop until a valid key is received and stored in `/psk.bin`.

---

## 4. Build System & Tooling

### PlatformIO Configuration (`platformio.ini`)
Configures the build for the `seeed_xiao_nrf52840_bluefruit` board. It links the `afantor/Bluefruit52_Arduino` library and the `Adafruit TinyUSB Library`.

### Build Hacks (`extra_script.py` & `include_fix/`)
A notable workaround for a C++ naming collision:
*   Both `Adafruit_LittleFS` and `SdFat` (included by TinyUSB for Mass Storage) declare a `File` class in the global namespace.
*   `extra_script.py` intercepts the compilation of the Bluefruit framework (specifically `bonding.cpp`) and forces it to use a dummy wrapper header (`include_fix/Adafruit_TinyUSB.h`) that omits the MSC/SdFat dependencies. This keeps `File` unambiguous across the project.