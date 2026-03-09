# Uguisu Battery Life Analysis — Extended Advertising Scenarios

## Current Hardware

| Parameter | Value |
|-----------|-------|
| Battery | TW-502535 (3.7V, 400mAh LiPo) |
| Standby draw | < 5 μA |
| Current config: per-press draw | ~0.004 mAh |
| Estimated life on 400mAh | 12–18 months (at ~50–100 presses/month) |

---

## Strategy: Expand Fob Advertising Window

Rather than increasing **scanner duty cycle** (which drains the scooter's main battery), shift burden to the **fob** (which is charged independently).

**Key assumption:** nRF52840 radio power scales mostly with **transmission time**, not packet count. Each 100 ms advertisement packet takes ~3–4 ms to transmit at typical BLE parameters.

---

## Scenario Comparison

| Config | Advertise Duration | Packet Count | Per-Packet Probability | Detection Rate | Est. mAh/Press | **Presses per Charge** | vs. Current |
|--------|-------------------|--------------|----------------------|-----------------|----------------|----------------------|-------------|
| **Current** | 2000 ms | 20 | 1% | ~18% | 0.004 | ~100k | baseline |
| **Option 1** | 3000 ms | 30 | 1% | ~26% | 0.006 | ~67k | -33% life |
| **Option 2** | 4000 ms | 40 | 1% | ~33% | 0.008 | ~50k | -50% life |
| **Option 3** | 5000 ms | 50 | 1% | ~39% | 0.010 | ~40k | -60% life |
| **Option 4** | 6000 ms | 60 | 1% | ~45% | 0.012 | ~33k | -67% life |

---

## Detection Probability Improvement

With extended advertising, detection probability improves according to:

```
P(detect) = 1 - (0.99)^N

where N = number of packets transmitted
```

| Packets | Probability |
|---------|------------|
| 20 | 18% |
| 30 | 26% |
| 40 | 33% |
| 50 | 39% |
| 60 | 45% |
| 70 | 50% |

**Key insight:** Each additional 10 packets (~1 second) costs ~0.002 mAh but gains ~6–8% detection probability.

---

## Practical Battery Life Estimates

Assuming **3 presses per day** at ~**30 μA standby** (nRF52840 system-off + XIAO charging IC quiescent).
This standby figure is back-calculated from the README's observed "12–18 months on 100 mAh LiPo."
The nRF52840 alone is <5 μA, but the XIAO module's charging IC adds ~25 μA quiescent.

Daily drain = (presses × mAh/press) + (30 μA × 24h = 0.72 mAh/day standby)

| Config | mAh/press | Daily press drain | Daily standby | Total daily | Days | Months |
|--------|-----------|-------------------|---------------|-------------|------|--------|
| Current (20 pkts) | 0.004 | 0.012 mAh | 0.72 mAh | 0.732 mAh | 547 | **18 mo** |
| +1s (30 pkts) | 0.006 | 0.018 mAh | 0.72 mAh | 0.738 mAh | 542 | **17.8 mo** |
| +2s (40 pkts) | 0.008 | 0.024 mAh | 0.72 mAh | 0.744 mAh | 538 | **17.7 mo** |
| +3s (50 pkts) | 0.010 | 0.030 mAh | 0.72 mAh | 0.750 mAh | 533 | **17.5 mo** |
| +4s (60 pkts) | 0.012 | 0.036 mAh | 0.72 mAh | 0.756 mAh | 529 | **17.4 mo** |

**Key finding: standby dominates.** Increasing from 20 to 60 packets costs only 2 weeks of battery life
across the full range. Press count is essentially irrelevant to Uguisu's battery life — choose packet
count purely for detection reliability, not power.

---

## Recommended Path Forward

### Option A: Conservative (+1s → 3s window, 30 packets)
- **Per-press draw:** ~0.006 mAh
- **Detection probability:** 26%
- **Battery life:** ~17.8 months at 3 presses/day
- **Verdict:** Easy win if 26% detection is tolerable for a security device

### Option B: Moderate (+2s → 4s window, 40 packets)
- **Per-press draw:** ~0.008 mAh
- **Detection probability:** 33%
- **Battery life:** ~17.7 months at 3 presses/day
- **Verdict:** Marginal reliability gain, negligible battery cost

### Option C: Aggressive (+3s → 5s window, 50 packets)
- **Per-press draw:** ~0.010 mAh
- **Detection probability:** 39%
- **Battery life:** ~1.8 months at 3 presses/day
- **Trade-off:** Noticeable charging frequency (~7 times/year)
- **Verdict:** Only worthwhile if detection rate truly critical

---

## Implementation

Update in `Uguisu/firmware/uguisu/include/uguisu_nrf52840.h`:

```c
// Current
#define UGUISU_ADVERTISE_MS 2000
#define UGUISU_ADV_INTERVAL_MS 100

// Option A: +1 second (30 packets)
#define UGUISU_ADVERTISE_MS 3000
#define UGUISU_ADV_INTERVAL_MS 100

// Option B: +2 seconds (40 packets)
#define UGUISU_ADVERTISE_MS 4000
#define UGUISU_ADV_INTERVAL_MS 100

// Option C: +3 seconds (50 packets)
#define UGUISU_ADVERTISE_MS 5000
#define UGUISU_ADV_INTERVAL_MS 100
```

No other code changes needed—the firmware already loops on advertising duration.

---

## Why This Works Better Than Increasing Scanner Duty

**Scooter (Guillemot):**
- 10 Ah main battery, always powered
- 1% → 4% duty cycle = 70 μA → 280 μA = **210 μA continuous increase**
- Over a week: 210 μA × 168 hrs = ~35 mAh drained
- On 10,000 mAh: negligible but persistent

**Fob (Uguisu):**
- 400 mAh battery, only active on button press (~3s every few hours)
- 0.004 mAh → 0.010 mAh per press = **0.006 mAh additional**
- At 3 presses/day: 0.018 mAh/day
- The fob is idle **99.99%** of the time anyway

**Asymmetry:** The scanner is *always on* (even at low duty cycle). The fob is *off almost always*. Shifting load to the fob exploits this asymmetry—the fob's idle power is negligible, so adding a few seconds of radio per press has minimal lifetime impact.

---

## Bottom Line

If you can tolerate ~30–40% detection probability on first press with current scanner settings, expanding the fob's advertising window to **4–5 seconds** (Option B/C) is a practical solution. You trade a modest reduction in fob battery life (still 2+ months of typical use) for a measurable improvement in user-facing reliability, while keeping the scooter's power consumption flat.

The alternative (increasing scanner duty to 4–20%) imposes a continuous drain on a much larger, shared battery that powers other systems—less elegant.
