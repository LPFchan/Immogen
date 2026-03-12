# Pipit UI/UX Design Session — Summary

*Date: 2026-03-11*

**Context:** Iterative design session defining the complete UI/UX architecture for the Pipit companion app, filling the last major gap in the master architecture doc (log 33). This session also surfaced and resolved a multi-user access control model that fed back into the core protocol design (Sections 2, 5, 6, 7 of log 33).

---

## 1. Foundation Decisions

Three initial design questions were resolved:

| Decision | Choice |
|---|---|
| Navigation model | Single-screen + modal sheets (utility app feel, not tab bar) |
| Primary interaction | Single toggle button (later evolved into 3D model) |
| Theme | System-follows (auto dark/light) |

---

## 2. 3D Uguisu Model as Primary Interface

The flat lock/unlock toggle button was replaced with a **photorealistic 3D model of the Uguisu hardware fob** as the home screen's sole interactive element. This reinforces the mental model that Pipit *is* the fob.

**Interaction:**
- **Tap** → Unlock (fire-and-forget AES-CCM payload)
- **Long press (700 ms)** → Lock (fires automatically at threshold, no release needed — matching `UGUISU_LONG_PRESS_MS` in firmware)

**Visual behaviour matches real Uguisu fire-and-forget:**
- LED is **always off at rest** — no persistent locked/unlocked visual distinction
- Unlock → green LED flash once (100 ms in, 200 ms hold, 200 ms out), button depresses, springs back
- Lock → red LED flash once, same timing
- No radial progress ring during long press (rejected as a design element)

**Rendering:** RealityKit (iOS) / SceneView+Filament (Android). `.usdz` + `.glb` assets. IBL environment lighting. LED is a separately addressable emissive mesh (`led_rgb`) controlled at runtime. Gyroscope parallax tilt (±5°, optional). Model depends on enclosure CAD (TBD); dev placeholder uses PCB dimensions.

**Haptics & sound:** Single heavy haptic at 700 ms lock threshold. Light haptic on tap unlock. Mechanical click sounds (~40–60 ms) mimicking SKQGABE010 tactile switch. Sound respects silent mode (`.ambient` session category on iOS, `getRingerMode()` check on Android).

---

## 3. Home Screen — Stripped Clean

All status text, slot labels, and proximity toggles were removed from the home screen. Only three elements remain:

1. **Gear icon** (top-left) → opens Settings
2. **3D Uguisu model** (center) → lock/unlock
3. **Hint label** ("Tap · Hold to lock") → shown until first lock, then hidden permanently

**Disconnect overlay:** When Guillemot is out of range, a system-theme-colored semi-transparent overlay (white 60% in light mode, black 60% in dark mode) covers the screen with "○ Disconnected" centered. Model visible but non-interactive beneath. Gear icon remains accessible above overlay.

**Settings transition:** Tapping the gear icon triggers a 3D flip animation — the Uguisu model rotates 180° on Y-axis, its back face cross-fades into the Settings content pane. Reverse on close.

---

## 4. Onboarding — Camera-First

Steps 1 (welcome) and 2 (QR scan) were merged. The app launches **directly into a full-screen camera** with a darkened overlay and a transparent QR-sized viewfinder window.

- Instruction text: *"Scan from Whimbrel"*
- Recovery link: *"recover key from lost phone >"* (bottom, subtle)
- Non-Immogen QR codes are **silently ignored** (no error toast)
- QR format detection determines flow branching:
  - Encrypted QR (`salt` + `ekey` fields) → PIN entry → Argon2id decryption
  - Plaintext QR (`key` field, guest provisioning) → skip PIN entirely

**QR decryption animation (~1s):** Dissolve (QR modules scatter as particles) → Convergence (particles re-converge, color-shift to accent) → Resolve (snap into key icon, haptic). KDF runs concurrently; animation pauses if hardware is slow.

**Location permission (iOS only):** Explains "Always Allow" requirement for iBeacon proximity unlock. Can be skipped.

**Done screen:** Shows all 4 slots (0–3) with tier labels (`OWNER` / `GUEST`) and the user's slot highlighted.

---

## 5. Access Tier Model

Emerged from a discussion about multi-user PIN management. The core realization: the 6-digit management PIN gates SMP pairing (BLE management access), but lock/unlock uses the Unlock/Lock characteristic (Write Without Response, no SMP, no PIN). Guest users who only lock/unlock never need the PIN.

### Slot Tiers

| Slot | Tier | Default Name | PIN Knowledge | Management Access |
|---|---|---|---|---|
| 0 | Hardware | `Uguisu` | N/A | None (USB-only via Whimbrel/Android OTG) |
| 1 | Owner | *(set during onboarding)* | Yes | Full |
| 2 | Guest | `Guest 1` | No | None (lock/unlock only) |
| 3 | Guest | `Guest 2` | No | None (lock/unlock only) |

Tier is implicit from slot number — no additional metadata stored.

### The `IDENTIFY` Command

Guillemot can't determine which slot is connecting for management from SMP alone (SMP proves PIN knowledge, not slot identity). New command added:

- Phone sends a 14-byte AES-CCM payload (same format as lock/unlock) with command byte `0x03` through the Management Command characteristic
- Guillemot decrypts using the claimed slot's key — if MIC valid + counter valid, session is bound to that slot
- All subsequent management commands are gated by the identified tier

| Command | Owner | Guest | No IDENTIFY |
|---|---|---|---|
| `IDENTIFY` | ✓ | ✓ | N/A |
| `SLOTS?` | ✓ | ✓ | ✓ (read-only) |
| `PROV` | ✓ | ✗ | ✗ |
| `REVOKE` | ✓ | ✗ | ✗ |
| `RENAME` (own slot) | ✓ | ✓ | ✗ |
| `RENAME` (other slot) | ✓ | ✗ | ✗ |

### Guest Provisioning — Unencrypted QR

Guest QR codes are **plaintext** (no Argon2id, no PIN):
```
immogen://prov?slot=2&key=<hex>&ctr=0&name=Guest%201
```

vs owner/migration (encrypted):
```
immogen://prov?slot=1&salt=<hex>&ekey=<encrypted_key_hex>&ctr=0&name=iPhone
```

The scanning phone distinguishes by field presence (`key` = plaintext, `ekey` + `salt` = encrypted). Security trade-off accepted: intercepted guest QR exposes the slot key, but (a) exchange is in-person, (b) owner can revoke at any time, (c) guest key grants no management access.

---

## 6. Settings — Owner vs Guest

Two distinct settings panes rendered based on the stored slot ID.

### Owner Settings (Slot 1)

| Section | Contents |
|---|---|
| **Proximity** | Background Unlock toggle, Unlock Distance slider, Lock Distance slider (10 dBm hysteresis enforced) |
| **Keys** (inline) | All 4 slots with tier labels. Slot 0: ⋮ Replace via USB (Android). Slot 1: own slot, no ⋮. Guest slots: ⋮ Rename/Replace/Delete. Empty slots: ⊕ → Provision Guest flow |
| **Device** | Transfer to New Phone, Change PIN via USB (Android), Flash Firmware via USB (Android) |
| **About** | Version, debug info on tap-hold |

### Guest Settings (Slot 2–3)

| Section | Contents |
|---|---|
| **Proximity** | Same as owner (phone-local settings) |
| **Your Key** | Single row: own slot name + "Transfer to New Phone" |
| **About** | Version |

No key management, no slot list, no USB operations, no PIN access.

---

## 7. Key Management Flows

### Provision Guest Phone (from ⊕ on empty slot)
Confirm → keygen → `PROV:<slot>:<key>:0:Guest N` → plaintext QR display. No name prompt (defaults to "Guest 1"/"Guest 2"), no PIN. One-tap flow.

### Replace (from ⋮ on active slot)
Confirm revocation → `REVOKE` → keygen → `PROV` → QR (plaintext for guest, encrypted for owner). Default name restored. Self-Provisioning Variant for "recover key from lost phone" onboarding path: connects to Guillemot via SMP, picks slot to replace, provisions self (no QR).

### Migration / Transfer to New Phone
Confirm → PIN entry (owner) or skip (guest) → QR display → "Done — I've Scanned" → confirm key deletion → key wiped, app returns to onboarding. QR includes current counter value for seamless takeover.

### Change PIN (Android only, USB serial)
Connect Guillemot via USB OTG → enter + confirm new 6-digit PIN → `SETPIN` via CDC serial. Existing SMP bonds unaffected; new PIN only applies to future pairing.

### USB Flashing (Android only)
Guillemot: UF2 mass storage via `libaums`. Uguisu: CDC serial firmware + key flash via `usb-serial-for-android`. Slot 0 replace (from ⋮ in key management) also routes through USB OTG.

---

## 8. Changes to Core Architecture (Sections 2, 5, 6, 7 of Log 33)

The UI/UX session surfaced protocol-level changes that were written back into the master architecture:

- **Section 2:** Slot tier table added (Hardware / Owner / Guest)
- **Section 5.5 (new):** `IDENTIFY` command spec, permission matrix, rationale for why SMP bonds alone are insufficient
- **Section 6.1:** `IDENTIFY` added to command transport table with tier gate column
- **Section 7.1.1 (new):** Guest provisioning with unencrypted QR, format differentiation (`key` vs `ekey`+`salt`)

All changes are reflected in log 33 as the single source of truth.
