# Response to Architectural Critique

This document addresses the points raised in `GUILLEMOT_ARCHITECTURE_CRITIQUE.md`, providing factual corrections, clarifying design trade-offs, and detailing the resolutions implemented.

## 1. Hardware Vulnerabilities
**Reviewer Claim:** The TPSM365R6V3RDNR buck converter has a maximum input voltage rating of 42V, posing a catastrophic failure risk from a 10S battery pack (42.0V).
**Status:** **Invalid.**

The reviewer's claim regarding the buck converter's specifications is incorrect. According to the official Texas Instruments datasheet, the TPSM365R6V3RDNR is a "3-V to 65-V Input, Synchronous Buck Converter Power Module". It is explicitly rated for a maximum continuous input voltage of **65V** and can withstand transient spikes up to **70V**.

A fully charged 10S battery pack rests at 42.0V. The 65V rating provides a comfortable 23V margin (154% headroom), which is more than sufficient to handle motor transients and regenerative braking spikes in this application. No hardware redesign is necessary for this component.

## 2. Firmware & User Experience
**Reviewer Claim:** The 1% duty cycle (20ms scan every 2000ms) introduces up to 2 seconds of latency, creating a sluggish user experience.
**Status:** **Valid (Design Trade-off).**

The reviewer correctly identifies the latency introduced by the duty cycle. However, this is a conscious and necessary design trade-off to achieve the ultra-low standby current (~22 µA). Without this aggressive duty cycling, the receiver would drain the scooter's battery while parked.

While 2 seconds of latency is noticeable, the fob is typically transmitting long before the user physically reaches the scooter's handlebars. We will investigate adaptive scanning models (e.g., dynamically decreasing the scan interval based on RSSI trends or incorporating an accelerometer) in a future hardware revision, but the current implementation remains optimal for power conservation.

## 3. Cryptography & Security
**Reviewer Claim:** The 32-bit (4-byte) AES-CCM MIC provides inadequate collision resistance (1-in-4.29-billion) for physical access control.
**Status:** **Valid.**

The reviewer makes a strong point regarding modern cryptographic standards. While the monotonic counter mitigates replay attacks, increasing the MIC length improves overall collision resistance against brute-force or spoofing attempts.

We have drafted a `MIC_UPGRADE_PROPOSAL.md` outlining the transition from a 4-byte to an 8-byte MIC. This upgrade is technically feasible within the BLE advertisement payload limits and will be implemented in the next coordinated firmware release across all repositories.

## 4. Storage & Reliability
**Reviewer Claim:** Log-based wear leveling on LittleFS is susceptible to corruption during power drops, especially during file rotation.
**Status:** **Invalid.**

The reviewer misunderstands the internal architecture of LittleFS. LittleFS is explicitly designed as a "fail-safe filesystem for microcontrollers" and provides strong resilience against power-loss corruption.

It achieves this through atomic updates using copy-on-write metadata pairs. Data is written to new blocks, and the metadata pointers are atomically swapped only after the write is successfully completed. If power is lost during a write or a rotation, the filesystem simply reverts to the last known-good state. The monotonic counter implementation (`CounterStore`) safely relies on this native transactional guarantee.

## 5. Build System & Tooling
**Reviewer Claim:** Using `extra_script.py` to intercept compilation and patch the `File` C++ naming collision is a fragile, brittle workaround.
**Status:** **Valid & Fixed.**

The reviewer correctly identified that intercepting include paths via Python scripting is a brittle approach that could easily break with future toolchain updates. 

**Resolution:** This has been permanently fixed. We have migrated away from the `extra_script.py` hack. The `ImmoCommon` library now includes `immo_tusb_config.h`, which natively uses the TinyUSB preprocessor macros (`CFG_TUD_MSC=0` and `CFG_TUH_MSC=0`) to cleanly disable the Mass Storage Class. This prevents the framework from pulling in the conflicting `SdFat` library, resolving the global `File` namespace collision at the source. This fix has been merged into `main`.