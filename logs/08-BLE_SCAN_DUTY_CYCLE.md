# BLE Scan Duty Cycle — Technical Analysis

*Date: 2026-03-09 19:53*

## Current Configuration

| Parameter | Value |
|-----------|-------|
| Scan interval (`SCAN_INTERVAL_MS`) | 2000 ms |
| Scan window (`SCAN_WINDOW_MS`) | 20 ms |
| Effective duty cycle | 1% |
| Fob ad interval (`UGUISU_ADV_INTERVAL_MS`) | 100 ms |
| Fob advertising duration (`UGUISU_ADVERTISE_MS`) | 2000 ms |
| Packets per button press | 20 |

## Detection Probability Model

Each advertisement packet has an independent probability of landing inside a scan window:

```
P(hit) = SCAN_WINDOW_MS / SCAN_INTERVAL_MS
P(miss all N packets) = (1 - P(hit))^N
P(detect) = 1 - P(miss all N)
```

Where `N = UGUISU_ADVERTISE_MS / UGUISU_ADV_INTERVAL_MS` = 20 packets per press.

## Options Evaluated

| Config | Duty | P(hit per packet) | P(detect in one press) | Expected presses | Avg scan current (est.) |
|--------|------|-------------------|------------------------|------------------|------------------------|
| Current: 2000/20 | 1% | 1% | **18%** | ~5.5 | ~70 μA |
| Option A: 1000/20 | 2% | 2% | **33%** | ~3.0 | ~140 μA |
| Option B: 1000/40 | 4% | 4% | **56%** | ~1.8 | ~280 μA |
| Reviewer: 200/40 | 20% | 20% | **99%** | ~1.0 | ~1.4 mA |

Current draw estimates scale linearly with duty cycle from the README's stated ~70 μA baseline at 1%.

## The Contention

**Reviewer's position:** 1% duty cycle causes ~5.5 expected presses per unlock. Recommends 200/40 ms (~99% single-press detection).

**User's position:** The 2-second advertising window is doing the heavy lifting — spreading 20 packets across 2 seconds means the scanner has multiple opportunities per press. Jumping to 200ms/40ms (20% duty) feels disproportionate and imposes unnecessary battery drain on the scooter.

**Where the math lands:** The user's intuition is directionally correct — the long advertising duration does compensate — but the current 1% duty is aggressive enough that the compensation is insufficient. With a 2-second scan interval you only open ~1 window per advertising burst, not multiple. The 20 packets only help if the scan window is narrow enough to create multiple independent chances, which requires the scan interval to be meaningfully shorter than the ad duration.

Specifically, the number of scan windows that open during a 2-second burst is:

```
windows = UGUISU_ADVERTISE_MS / SCAN_INTERVAL_MS = 2000 / 2000 = 1
```

At 1000 ms scan interval that becomes 2 windows, which is where Options A and B show improvement.

## Open Question

The decision reduces to an acceptable UX trade-off:

- **Option A (1000/20, 2%):** ~3 presses average — noticeable but tolerable. Minimal power increase.
- **Option B (1000/40, 4%):** ~1.8 presses average — close to "works first try most of the time." Still negligible on a 10 Ah battery (would last ~4 years at 280 μA continuous).
- **Reviewer's 200/40 (20%):** Essentially guaranteed single press. ~1.4 mA is still a rounding error on the scooter battery, but structurally it's a 20× increase in duty cycle for a security-adjacent always-on receiver.

The key question is whether "works ~56% of the time on first press" (Option B) is acceptable for an immobilizer fob, or whether the failure-to-detect UX for a security device warrants the higher duty cycle. No code changes made pending decision.
