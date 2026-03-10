# Monorepo Migration Report — Immogen

*Date: 2026-03-10 00:04*

This document summarizes how the ImmoCommon → Immogen monorepo migration was executed in practice, based on the [12-MONOREPO_MIGRATION_GUIDE.md](12-MONOREPO_MIGRATION_GUIDE.md) and the actual session.

---

## Executive Summary

- **ImmoCommon** was restructured, merged with **Guillemot** and **Uguisu** via `git subtree`, and renamed to **Immogen**.
- Git history was preserved for all three projects.
- A single release workflow now builds both firmwares and produces four artifacts per tag.
- The standalone Guillemot and Uguisu repos were archived on GitHub.
- First release: **v0.1.0**.

---

## Execution Overview

### Approach

Migration was done **in-place** in the existing workspace (not in a fresh clone). Local Guillemot and Uguisu repos were used with `git subtree add` instead of fetching from GitHub:

```bash
git subtree add --prefix=Guillemot ../Guillemot main
git subtree add --prefix=Uguisu ../Uguisu main
```

This preserved full history without network fetches.

### Phases Completed

| Phase | Description | Status |
|-------|-------------|--------|
| 1 | Restructure ImmoCommon (src → lib/) | ✓ |
| 2 | Add Guillemot, flatten firmware, remove submodule | ✓ |
| 3 | Add Uguisu, flatten firmware, consolidate tools | ✓ |
| 4 | Single release workflow (both firmwares) | ✓ |
| 5 | Rename to Immogen, update docs | ✓ |
| 6 | Update Whimbrel for Immogen | ✓ |
| 7 | Build both locally, push | ✓ |
| 8 | Archive old repos, update links | ✓ |

---

## Structural Changes (Beyond the Guide)

### lib/ Flattening

The guide assumed `lib/src/` and `lib/tools/`. During migration we flattened further:

1. **lib/src → lib** — Shared C++ sources moved directly to `lib/` (no `src` subfolder).
2. **tools/ consolidation** — `lib/tools/test_vectors/` was moved to `tools/test_vectors/` at repo root, alongside the HTML tools (led_visualizer, ble_timing_simulator, buzzer_tuner).

### Final Layout

```
Immogen/
├── lib/
│   ├── ImmoCommon.h
│   ├── immo_crypto.cpp
│   ├── immo_provisioning.cpp
│   ├── immo_storage.cpp
│   ├── immo_util.cpp
│   ├── immo_tusb_config.h
│   ├── library.json          # PlatformIO manifest
│   └── ...
├── Guillemot/
│   ├── firmware/             # PlatformIO project (flattened from firmware/guillemot/)
│   └── ...
├── Uguisu/
│   ├── firmware/             # PlatformIO project (flattened from firmware/uguisu/)
│   └── ...
├── tools/
│   ├── led_visualizer.html
│   ├── ble_timing_simulator.html
│   ├── buzzer_tuner.html
│   └── test_vectors/
│       ├── gen_mic.py
│       └── README.md
├── logs/
├── .github/workflows/release.yml
└── .gitignore
```

---

## Build Fixes

### Problem

After flattening `lib/src` and removing submodules, builds failed with:

- `undefined reference to immo::*` — lib sources were not compiled.
- `immo_tusb_config.h: No such file or directory` — include path missing for TinyUSB.

### Solution

1. **lib/library.json** — Added a PlatformIO manifest so `lib/` is treated as a library:

   ```json
   {
     "name": "ImmoCommon",
     "version": "0.1.0",
     "description": "Shared crypto, provisioning, and storage for Immogen BLE immobilizer (Ninebot G30)",
     ...
   }
   ```

2. **lib_deps with symlink** — Both firmwares use:

   ```ini
   lib_deps =
     ...
     ImmoCommon=symlink://../../lib
   ```

3. **Include path** — Kept `-I ../../lib` in `build_flags` so TinyUSB can find `immo_tusb_config.h`.

---

## Rename and Repo Updates

### GitHub Repo Rename

```bash
gh repo rename Immogen --repo LPFchan/ImmoCommon
git remote set-url origin https://github.com/LPFchan/Immogen.git
```

### Local Folder Rename

ImmoCommon folder was manually renamed to Immogen (outside Git). Git does not track the parent folder name.

---

## Author Privacy Rewrite

The first 9 commits exposed a real name via author metadata. These were rewritten using `git filter-branch`:

1. Identified the first 9 commit hashes.
2. Used `--env-filter` to set `GIT_AUTHOR_*` and `GIT_COMMITTER_*` to `LPFchan <me@lost.plus>` for those commits.
3. Force-pushed the rewritten history.

**Note:** Any future history rewrites should prefer `git filter-repo` over `filter-branch` (which is deprecated).

---

## Archiving Old Repos

```bash
gh repo archive LPFchan/Guillemot
gh repo archive LPFchan/Uguisu
```

Both repos are now read-only and marked as archived on GitHub.

---

## Release Workflow

### First Release: v0.1.0

- An initial **v1.0.0** was created, then retracted.
- Tag **v0.1.0** was created and pushed.
- The `.github/workflows/release.yml` triggers on `v*` tags and builds both firmwares.
- Artifacts: `guillemot-0.1.0.{hex,zip}`, `uguisu-0.1.0.{hex,zip}`.

### Retracting a Release

```bash
gh release delete v1.0.0 --repo LPFchan/Immogen --yes
git push origin --delete v1.0.0
```

---

## Whimbrel Updates

Whimbrel (companion web app) was updated to fetch releases from a single repo:

- **config.js**: `GITHUB_REPO: "Immogen"`
- **firmware.js**: Asset selection by device type (`guillemot-*.zip` vs `uguisu-*.zip`), both fetching from Immogen.
- **index.html**: Tagline updated to point to Immogen.

---

## Cleanup Done

- Deleted `rewrite-authors.sh` (one-off script).
- Added `.gitignore` with `.DS_Store`.
- Moved `logs/` from workspace root into Immogen.
- Updated Whimbrel README links to Immogen.

---

## Checklist (Final)

- [x] Phase 1: Restructure ImmoCommon (src → lib/)
- [x] Phase 2: Add Guillemot, flatten firmware, remove submodule
- [x] Phase 3: Add Uguisu, flatten firmware, consolidate tools
- [x] Phase 4: Single release workflow
- [x] Phase 5: Rename to Immogen, update docs
- [x] Phase 6: Update Whimbrel
- [x] Phase 7: Build both, verify, push
- [x] Phase 8: Archive Guillemot and Uguisu, update links
- [x] Author rewrite for first 9 commits
- [x] Release v0.1.0

---

## References

- [12-MONOREPO_MIGRATION_GUIDE.md](12-MONOREPO_MIGRATION_GUIDE.md) — Original migration guide
- [Immogen](https://github.com/LPFchan/Immogen) — Monorepo
- [Whimbrel](https://github.com/LPFchan/Whimbrel) — Companion web app
