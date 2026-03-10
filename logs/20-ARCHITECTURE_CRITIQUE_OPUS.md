# Immogen Architecture Critique

*Author: Opus*
*Date: 2026-03-10 09:00*

---

## Cryptography

### `verify_payload()` may not check `ccm_mic_8()` return value

`ccm_mic_8()` returns `false` when `sd_ecb_block_encrypt()` fails (SoftDevice busy during a BLE event). If `verify_payload()` doesn't check this, the MIC comparison runs against an uninitialized or stale buffer, which could randomly match a received MIC. This is a potential authentication bypass.

**Fix:** Check the return value; reject the payload immediately on failure.

### Nonce wastes 9 bytes that should hold a session diversifier

The nonce is `LE32(counter) || 0x00 * 9`. If the same key is ever re-provisioned with a counter reset (bug, manual intervention, flash corruption), nonce reuse breaks CCM entirely. A random session ID generated during provisioning and stored alongside the key would close this.

### Payload is authenticated but not encrypted

Counter and command are sent in plaintext. Any passive BLE sniffer sees when you lock/unlock, which command was sent, and your counter value (reveals usage frequency). Full CCM encryption is free — the implementation already does the counter-mode work for the tag but doesn't apply the keystream to the plaintext.

### Manual CCM instead of hardware

The code manually constructs B0 blocks and does CBC-MAC using `sd_ecb_block_encrypt()`. The nRF52840 has a dedicated CCM peripheral. The SoftDevice restricts direct access, but its own CCM API (`sd_ecb_block_encrypt` is already a SoftDevice call) may offer a higher-level path. Fewer lines of manual crypto = fewer places to get it wrong.

---

## Anti-Replay

### Counter log rotation has a power-failure vulnerability

`rotateIfNeeded_()` does: delete old log → rename current → old. If power is lost after the rename but before `update()` writes to the new current log:

1. On next boot, `load()` reads the current log — file doesn't exist — `last_counter_` = 0
2. `load()` does **not** read the old log file
3. Guillemot now accepts any counter > 0, including replays of previously-accepted values

**Fix:** `load()` should scan both current and old log files, taking the maximum counter from both.

---

## Firmware

### No watchdog on Guillemot

Guillemot is an always-on, safety-critical receiver. If the firmware hangs (SoftDevice error, flash corruption, radio lockup), the scooter stays in whatever state it was in — potentially unlocked indefinitely. `sd_app_evt_wait()` in `loop()` sleeps forever if events stop arriving. The nRF52840 WDT should be configured; the SoftDevice supports WDT coexistence.

### Button press timing offset

`wait_for_button_press_release()` runs after boot initialization (~100-200 ms). The button press that woke the device from system-off is already in progress. Measured press duration is shorter than actual by the boot time. The 1000 ms long-press threshold for Lock actually requires ~1200 ms of physical holding. Either compensate for boot time or document the effective threshold.

### Button timeout is excessive

10 seconds awake at ~5 mA waiting for a button press that already triggered the wake. 2-3 seconds is sufficient. Saves ~35-40 mJ per accidental wake.

### FreeRTOS task for provisioning LED is overkill

Uguisu spawns a FreeRTOS task with 512 bytes of stack just to blink a blue LED. A timer interrupt or non-blocking `millis()`-based approach uses zero stack and is simpler.

---

## Hardware — Guillemot

### No reverse polarity protection

If the XT60 battery pigtail is soldered backwards, the N-FET body diode conducts and dumps reverse voltage into the buck converter and MCU. A series P-FET ideal diode or Schottky on the input would prevent this. Hand-soldered pigtails in a scooter environment make polarity reversal a real risk.

### TVS clamping voltage may exceed downstream ratings

SMBJ45A clamps at 72.7 V. The TPSM365R6V3RDNR has a 65 V abs max input. The 12 V Zener (D1) in the path between the TVS and the buck input should limit this, but the Zener's current handling during transient events needs verification — if it can't shunt the full transient current, the buck sees overvoltage.

### Push-pull buzzer driver uses two NPNs

A standard complementary push-pull uses NPN + PNP. Two MMBT3904 (both NPN) in totem-pole means the high-side transistor loses ~1.5 V (`Vce_sat + Vbe`), reducing drive voltage to the buzzer. A PNP high-side or a single N-FET low-side switch would deliver full rail voltage and be simpler.

### No ESD protection on signal pins

XIAO GPIO pins (D0, D1, D3) connect directly to external components with no TVS or series resistance. Scooter environment = metal frame, outdoor exposure, ESD events. Small TVS diodes (e.g., PESD5V0S2BT) on latch and buzzer lines would add robustness.

### No conformal coating specified

The board sits on a scooter deck exposed to rain, road spray, and vibration. Conformal coating or potting should be specified in production documentation.

---

## BLE Protocol

### No delivery confirmation

The fob broadcasts for 600 ms and sleeps with no acknowledgment. If out of range or if interference blocks the signal, the user gets zero feedback at the fob. The buzzer confirmation is only at the receiver end. For a security-critical device, a missed command is indistinguishable from a successful one at the point of use.

### Company ID 0xFFFF causes unnecessary processing

0xFFFF is the Bluetooth SIG test/unregistered ID. Any other device using it triggers `parse_payload_from_report()` and a full MIC check before rejection. A registered company ID or a magic byte prefix in the MSD payload would filter earlier.

---

## Recommendations (if rebuilding)


| Area     | Change                                             |
| -------- | -------------------------------------------------- |
| Crypto   | Use full CCM encryption, not auth-only             |
| Crypto   | Add random session diversifier to nonce bytes 4-12 |
| Crypto   | Check `ccm_mic_8()` return value in all call sites |
| Storage  | Read both current + old log in `load()`            |
| Firmware | Add WDT on Guillemot                               |
| Firmware | Reduce button timeout to 2-3 s                     |
| Firmware | Compensate long-press threshold for boot time      |
| Hardware | Add reverse polarity protection                    |
| Hardware | Verify TVS/Zener transient current path            |
| Hardware | Add ESD protection on exposed GPIO                 |
| Hardware | Specify conformal coating                          |


