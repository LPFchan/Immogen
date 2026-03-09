# Simulating BLE Detection Probability for a DIY Scooter Immobilizer

*A deep dive into why "100% detection" on paper can still miss in the real world, and how a browser-based timing simulator exposed the truth.*

---

## Background

This is part of an ongoing project to build a cryptographic BLE immobilizer for the Ninebot G30 electric scooter. The system has two custom boards:

- **Guillemot** — the scanner/receiver, fitted to the scooter. It runs continuously on the scooter's 36 V battery, passively listening for BLE advertisements that match a known manufacturer-specific data (MSD) pattern.
- **Uguisu** — the fob/transmitter, carried by the rider. On button press, it broadcasts a rolling-code BLE advertisement burst for a fixed duration, then goes back to sleep.

Both boards are built around the Seeed XIAO nRF52840 (Nordic nRF52840 SoC, Adafruit Bluefruit52 library, PlatformIO/Arduino framework).

The security model is intentionally simple at the radio layer: detection is a timing problem, not a cryptographic one. The crypto is layered on top via AES-128 CCM-8 with a monotonic counter. This post focuses entirely on the timing problem.

---

## The BLE Timing Model

BLE advertising works by interleaving two periodic clocks:

```
Uguisu (fob):    ──■──────■──────■──────■──────■──────■──────■──   (packets every advInterval ms)
Guillemot (scan):  ├──┤      ├──┤      ├──┤      ├──┤      ├──┤    (window open every scanInterval ms)
                   ↑  ↑
               window  window
               start   end
```

A **packet is detected** when a Uguisu advertisement lands inside an open Guillemot scan window. The scan window is `scanWindow` ms wide, repeating every `scanInterval` ms.

The key parameters:

| Parameter | Symbol | Our value |
|---|---|---|
| Scan interval | `scanInterval` | 500 ms |
| Scan window | `scanWindow` | 20 ms |
| Advertise duration | `advDuration` | 600 ms |
| Packet interval | `advInterval` | 20 ms |
| Total packets | `N = floor(advDuration / advInterval) + 1` | 31 |

---

## The Misleading Formula

The standard textbook formula for BLE detection probability is:

```
p(hit per packet) = scanWindow / scanInterval
P(detect)         = 1 - (1 - p)^N
```

With our parameters:

```
p     = 20 / 500 = 0.04  (4%)
N     = 31
P     = 1 - 0.96^31 ≈ 71%
```

This formula assumes packet and window phases are **independent and uniformly random** on every packet — essentially, that the radio randomly repositions itself between each packet. But BLE clocks are deterministic. Once you press the button, both clocks run at fixed rates. The phase relationship between them is fixed for the entire advertisement burst.

In practice, if the scan window happens to fall squarely in the gap between two consecutive packets for the entire burst duration — you get **zero detections**, regardless of what the formula says.

---

## The Phase Insight

Think of the problem geometrically. The fob emits packets at positions `0, T, 2T, 3T, ...` on the timeline. The scanner opens a window of width `W` every `I` ms, starting at some offset `φ` relative to the button press.

That offset `φ` is the **phase**: it ranges over `[0, scanInterval)` and is determined entirely by where in its scan cycle the scooter happened to be when you pressed the button. You have no control over it. Each button press lands at a different, effectively random phase.

For a given phase `φ`, detection is either successful or not — there is no probability at that level. The stochasticity is in `φ`, not in the radio.

The correct way to compute detection probability is:

> **What fraction of all possible phase values result in at least one packet being detected?**

This is a geometric covering problem. Sample 500 evenly-spaced phases across one full `scanInterval`, run the deterministic simulation for each, and count successes.

---

## The Simulator

To make this concrete and interactive, a browser-based simulator was built: `tools/ble_timing_simulator.html`. It is a single self-contained HTML file (no bundler, no framework — only Google Fonts as an external dependency) that renders a live timeline canvas and computes phase statistics on every slider change.

### Core simulation function

```javascript
function buildSim(p, phase) {
  // Generate all packet timestamps
  const packets = [];
  for (let t = 0; t <= p.advDuration; t += p.advInterval) packets.push(t);

  // Generate all scan windows, offset by phase
  const windows = [];
  for (let t = -phase; t <= p.advDuration; t += p.scanInterval) {
    const ws = t, we = t + p.scanWindow;
    if (we > 0 && ws <= p.advDuration) windows.push({ start: ws, end: we });
  }

  // Intersect packets with windows
  const detected = new Set();
  let firstHit = null;
  for (const pkt of packets) {
    for (const w of windows) {
      if (pkt >= w.start && pkt <= w.end) {
        detected.add(pkt);
        if (firstHit === null) firstHit = pkt;
        break;
      }
    }
  }

  return { packets, windows, detected, firstHit };
}
```

`firstHit` is the timestamp (ms after button press) of the first detected packet — the detection latency for that phase. A `null` firstHit means complete miss.

### Phase statistics

```javascript
function calcPhaseStats(p) {
  const SAMPLES = 500;
  let vfast = 0, fast = 0, slow = 0, vslow = 0, miss = 0;
  let worstHit = null;

  for (let i = 0; i < SAMPLES; i++) {
    const phase = (i / SAMPLES) * p.scanInterval;
    const { firstHit } = buildSim(p, phase);

    if      (firstHit === null)  miss++;
    else if (firstHit <= 250)    vfast++;
    else if (firstHit <= 500)    fast++;
    else if (firstHit <= 1000)   slow++;
    else                         vslow++;

    if (firstHit !== null)
      worstHit = worstHit === null ? firstHit : Math.max(worstHit, firstHit);
  }

  return { vfast, fast, slow, vslow, miss, total: SAMPLES, worstHit };
}
```

The displayed **P(detect)** is `(vfast + fast + slow + vslow) / 500` — the true phase-averaged detection rate.

### Statistics panel

The simulator shows seven stats at a glance:

| Stat | Description |
|---|---|
| P(hit / packet) | `scanWindow / scanInterval` — geometric overlap per packet |
| Total Packets | `N = floor(advDuration / advInterval) + 1` |
| P(detect) | Fraction of 500 phases that result in ≥ 1 detection |
| P(late response) | Of successful detections, fraction with `firstHit > 1000 ms` |
| Worst response | Maximum `firstHit` across all 500 phases (or MISS if any phase fails) |
| Expected Presses | `1 / P(detect)` — average button presses needed for one success |
| This Press | HIT or MISS result of the last animated simulation |

---

## Key Discoveries

### 1. Sweet spots exist

With tightly-packed packets (e.g., 20 ms interval) and a wide-enough window, there exist parameter combinations where **all 500 phases succeed**. The scan window is wide enough that it always overlaps with at least one packet, regardless of phase. These are "dead-zone-free" configurations.

Conversely, with widely-spaced packets and a narrow window, there are phase ranges that always miss — the window falls cleanly in the gap between every pair of packets for the whole burst duration.

### 2. The formula breaks down at low duty cycles

With `scanInterval = 2000`, `scanWindow = 20` (1% duty cycle) and `advInterval = 100`, the formula gives ~71% detection. The phase simulation gives a very different, lower number because there are real dead-zone phases. Pressing the button repeatedly at the same phase (which happens when you press quickly and consistently) reliably fails.

### 3. Worst case is the design constraint

For a security device, the worst case matters more than the average. The simulator's "Worst response" and "MISS" indicators make worst-case design explicit. With our chosen parameters (500/20 scan, 600/20 advertise, 31 packets), all 500 phases succeed with a worst-case latency well under 600 ms.

### 4. Phase slider as a debugging tool

The phase offset slider (0–499) allows scrubbing through all sampled phases manually. This was directly useful: when a particular button-press position caused consistent failures in hardware testing, the slider could be moved to find the exact worst-case phase and visualise why it failed in the timeline.

---

## Timing Colour Scheme

Detection latency is colour-coded consistently across all UI elements (scan windows, packet bars, banners, stats):

| Colour | Range | Meaning |
|---|---|---|
| 🔵 Blue `#58a6ff` | < 250 ms | Very fast |
| 🟢 Green `#3fb950` | 250–500 ms | Fast |
| 🟡 Amber `#d29922` | 500–1000 ms | Slow |
| 🟠 Orange `#f0883e` | > 1000 ms | Very slow |
| ⬜ Gray `#484f58` | — | Miss |

The phase distribution bar (iOS storage-bar style, pill-shaped) shows the split across all 500 phases at a glance. It uses `Path2D.roundRect()` clipping to achieve the pill shape without per-segment border-radius logic.

---

## Hardware Constraints

### nRF52840 SoftDevice minimum advertising interval

The Nordic SoftDevice S140 enforces a hard minimum of **20 ms** (32 BLE units × 0.625 ms/unit) for legacy advertising. Attempting to set a 10 ms interval results in the SoftDevice clamping or rejecting the value. The Adafruit Bluefruit52 `setInterval()` API takes raw BLE units, computed as:

```cpp
// Convert ms → BLE units (round to nearest)
uint16_t units = (UGUISU_ADV_INTERVAL_MS * 8 + 4) / 5;
Bluefruit.Advertising.setInterval(units, units);
```

For 20 ms: `(20 × 8 + 4) / 5 = 32 units` ✓
For 10 ms: `(10 × 8 + 4) / 5 = 16 units` — below the 32-unit floor, rejected.

### Guillemot scan window

The BLE spec allows scan windows as narrow as 2.5 ms (4 units). Our 20 ms scan window is well above the floor and is fully supported by S140.

### Power budget

At 500 ms interval / 20 ms window (4% duty cycle), Guillemot draws approximately:

```
I = 5 μA (static) + 17 μA × (0.04 / 0.01) = 5 + 68 = 73 μA
```

From the Ninebot G30's 15.3 Ah / 36 V battery pack, this gives a standby lifetime measured in years — negligible against the scooter's own quiescent draw.

Uguisu at 31 packets per press draws ~6.2 μAh per button press (0.0002 mAh/packet × 31). With 3 presses/day and 30 μA system-off standby current on the nRF52840, battery life is dominated entirely by standby and exceeds several years on a 400 mAh LiPo.

---

## Final Parameters

```c
// guillemot_nrf52840.h
static constexpr uint16_t SCAN_INTERVAL_MS = 500;  // ms
static constexpr uint16_t SCAN_WINDOW_MS   = 20;   // ms  (4% duty cycle)

// uguisu_nrf52840.h
#define UGUISU_ADVERTISE_MS    600   // ms  (total burst duration)
#define UGUISU_ADV_INTERVAL_MS  20   // ms  (minimum on S140)
```

At these settings:
- **31 packets** transmitted per press
- **P(detect) ≈ 96–100%** depending on exact phase (all 500 sampled phases succeed in simulation)
- **Worst-case latency < 600 ms** (bounded by advertise duration)
- **Guillemot duty cycle: 4%** — well within scooter battery budget

---

## Takeaway

The standard BLE detection formula `1 - (1-p)^N` is a useful upper-bound estimate but is structurally misleading for fixed-clock BLE advertising. Real detection probability depends on the phase relationship between two deterministic clocks, not on independent per-packet randomness.

The correct mental model is geometric: given the advertise packet train and the scan window comb, what fraction of all phase offsets result in at least one overlap? Simulating 500 evenly-spaced phases gives an accurate empirical answer in milliseconds of compute time, and makes the worst-case response time directly visible — which is what actually matters when designing a security-critical device.
