# Guillemot Architecture: Critical Review & Areas for Improvement

This document highlights the specific architectural, hardware, and firmware design choices in the Guillemot receiver that require review, redesign, or optimization. All adequately designed components have been omitted to focus strictly on vulnerabilities and inefficiencies.

## 1. Hardware Vulnerabilities

*   **Buck Converter Headroom:** The TPSM365R6V3RDNR buck converter has a maximum input voltage rating of 42V. A Ninebot Max G30 uses a 10S battery pack, which rests at 42.0V when fully charged. Voltage spikes from motor transients or regenerative braking will easily exceed 42V, risking immediate catastrophic failure of the buck converter.
    *   *Recommendation:* Replace the buck converter with a component rated for at least 60V (e.g., standard e-bike/scooter DC-DC specs) or implement aggressive transient voltage suppression (TVS) clamping.

## 2. Firmware & User Experience

*   **BLE Scan Latency:** The 1% duty cycle (20ms window every 2000ms) minimizes power consumption but introduces up to a 2-second latency before a fob is recognized. This will likely result in a sluggish user experience where the scooter fails to immediately unlock upon approach.
    *   *Recommendation:* Implement adaptive scanning (e.g., higher duty cycle triggered by an onboard IMU/accelerometer when the scooter is touched) or decrease the scan interval to ~500ms.

## 3. Cryptography & Security

*   **MIC Truncation:** The AES-CCM Message Integrity Code (MIC) is truncated to 4 bytes (32 bits). While mitigating replay attacks via the monotonic counter, a 32-bit MIC provides only a 1-in-4.29-billion chance of collision. This is below modern NIST recommendations for secure physical access.
    *   *Recommendation:* Increase the MIC length to 8 bytes (64 bits) if the BLE advertisement payload size permits.

## 4. Storage & Reliability

*   **Log-based Wear Leveling (LittleFS):** Appending counter records to a LittleFS `/ctr.log` file and manually rotating it at 4096 bytes is highly susceptible to corruption if the power drops precisely during a flash write or rotation.
    *   *Recommendation:* Transition away from a filesystem-based log for monotonic counters. Use Nordic's native Flash Data Storage (FDS) or a raw flash circular buffer designed specifically for atomic, power-safe counter increments.

## 5. Build System & Tooling

*   **Brittle Header Interception Hack:** Using `extra_script.py` to intercept the Adafruit Bluefruit framework compilation to patch the `File` C++ naming collision is a fragile workaround. Any future update to the Adafruit nRF52 core or TinyUSB library is highly likely to break the build.
    *   *Recommendation:* Resolve the dependency conflict properly by avoiding the global namespace collision (e.g., explicitly scoping namespaces) or migrate the project to the more robust Zephyr RTOS (nRF Connect SDK) instead of relying on the Arduino core for a production security device.