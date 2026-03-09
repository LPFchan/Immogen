# Immogen Monorepo Migration Guide

This guide describes how to merge Guillemot and Uguisu into the ImmoCommon repository, rename it to **Immogen**, and reconfigure releases. **Full git history is preserved** for all projects.

## Summary of changes

| Item | Before | After |
|------|--------|-------|
| Repo name | ImmoCommon | **Immogen** |
| Releases | Separate per project | **Single release** with both firmwares |
| Release artifacts | firmware.zip, firmware.hex per repo | guillemot-1.0.0.zip, guillemot-1.0.0.hex, uguisu-1.0.0.zip, uguisu-1.0.0.hex |
| Internal code | immo::, ImmoCommon.h | **Unchanged** (Option A) |

---

## Prerequisites

- Git 2.23+ (for `git subtree`)
- Clean working tree in all three repos
- Push any pending commits before starting
- **Back up** your work

---

## Target Layout

Use **uppercase** `Guillemot` and `Uguisu`; flatten firmware paths to `/Guillemot/firmware` and `/Uguisu/firmware` (no extra `guillemot/` or `uguisu/` subfolder).

```
Immogen/                              # single repo (renamed from ImmoCommon)
├── lib/                              # shared library (current ImmoCommon src)
│   ├── src/
│   │   ├── immo_crypto.cpp
│   │   ├── immo_provisioning.cpp
│   │   └── ...
│   └── tools/
├── Guillemot/                        # receiver (from Guillemot repo)
│   ├── firmware/                     # platformio project directly here
│   ├── *.kicad_*
│   └── ...
├── Uguisu/                           # fob (from Uguisu repo)
│   ├── firmware/                     # platformio project directly here
│   ├── *.kicad_*
│   └── ...
├── tools/                            # consolidated HTML tools from both projects
│   ├── led_visualizer.html            # from Uguisu
│   ├── ble_timing_simulator.html      # from Uguisu
│   └── buzzer_tuner.html              # from Guillemot
└── .github/
    └── workflows/
        └── release.yml
```

---

## Phase 1: Prepare ImmoCommon (pre-rename)

Work in a **fresh clone** to avoid touching your main workspace.

```bash
cd /tmp
git clone https://github.com/LPFchan/ImmoCommon.git immogen-monorepo
cd immogen-monorepo
```

### 1.1 Restructure: move src and tools into lib/

```bash
mkdir -p lib
git mv src lib/
git mv tools lib/ 2>/dev/null || true
git add -A
git commit -m "Restructure: move src and tools into lib/ for monorepo layout"
```

---

## Phase 2: Add Guillemot with History

### 2.1 Add Guillemot subtree

```bash
git subtree add --prefix=Guillemot https://github.com/LPFchan/Guillemot.git main
```

### 2.2 Flatten firmware directory

```bash
mv Guillemot/firmware/guillemot/* Guillemot/firmware/
rm -rf Guillemot/firmware/guillemot
```

### 2.3 Remove Guillemot's ImmoCommon submodule

```bash
rm -rf Guillemot/firmware/lib/ImmoCommon
```

Delete `Guillemot/.gitmodules` (or remove the ImmoCommon entry) so the integrated tree doesn't appear as a submodule:

```bash
rm -f Guillemot/.gitmodules
```

### 2.4 Update Guillemot platformio.ini

Edit `Guillemot/firmware/platformio.ini`:

**Change:** Replace `-I lib/ImmoCommon/src` with `-I ../../lib/src`

### 2.5 Commit

```bash
git add Guillemot/
git commit -m "Integrate Guillemot: flatten firmware, remove lib submodule, point to in-tree lib"
```

---

## Phase 3: Add Uguisu with History

### 3.1 Add Uguisu subtree

```bash
git subtree add --prefix=Uguisu https://github.com/LPFchan/Uguisu.git main
```

### 3.2 Flatten firmware directory

```bash
mv Uguisu/firmware/uguisu/* Uguisu/firmware/
rm -rf Uguisu/firmware/uguisu
```

### 3.3 Remove Uguisu's ImmoCommon submodule

```bash
rm -rf Uguisu/firmware/lib/ImmoCommon
```

Delete `Uguisu/.gitmodules` (or remove the ImmoCommon entry):

```bash
rm -f Uguisu/.gitmodules
```

### 3.4 Update Uguisu platformio.ini

Edit `Uguisu/firmware/platformio.ini`:

**Change:** Replace `-I lib/ImmoCommon/src` with `-I ../../lib/src`

### 3.5 Consolidate tools to repo root

Move the three HTML tools to the new `tools/` directory at repo root:

```bash
mkdir -p tools
mv Uguisu/tools/led_visualizer.html tools/
mv Uguisu/tools/ble_timing_simulator.html tools/
mv Guillemot/tools/buzzer_tuner.html tools/
```

### 3.6 Commit

```bash
git add Uguisu/ Guillemot/ tools/
git commit -m "Integrate Uguisu: flatten firmware, remove submodule, consolidate tools to /tools"
```

---

## Phase 4: Single Release Workflow (Both Firmwares)

One tag (e.g. `v1.0.0`) builds **both** Guillemot and Uguisu and creates a single release with four artifacts:
- `guillemot-1.0.0.hex`
- `guillemot-1.0.0.zip`
- `uguisu-1.0.0.hex`
- `uguisu-1.0.0.zip`

### 4.1 Create `.github/workflows/release.yml`

```yaml
name: Release

permissions:
  contents: write

on:
  push:
    tags:
      - 'v*'

jobs:
  build-and-release:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'

      - name: Install PlatformIO
        run: pip install -U platformio

      - name: Build Guillemot
        working-directory: Guillemot/firmware
        run: pio run

      - name: Build Uguisu
        working-directory: Uguisu/firmware
        run: pio run

      - name: Rename artifacts for release
        run: |
          V=${GITHUB_REF#refs/tags/v}
          cp Guillemot/firmware/.pio/build/seeed_xiao_nrf52840_bluefruit/firmware.hex guillemot-${V}.hex
          cp Guillemot/firmware/.pio/build/seeed_xiao_nrf52840_bluefruit/firmware.zip guillemot-${V}.zip
          cp Uguisu/firmware/.pio/build/seeed_xiao_nrf52840_bluefruit/firmware.hex uguisu-${V}.hex
          cp Uguisu/firmware/.pio/build/seeed_xiao_nrf52840_bluefruit/firmware.zip uguisu-${V}.zip

      - name: Create release
        uses: softprops/action-gh-release@v2
        with:
          tag_name: ${{ github.ref_name }}
          name: ${{ github.ref_name }}
          files: |
            guillemot-*.hex
            guillemot-*.zip
            uguisu-*.hex
            uguisu-*.zip
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

### 4.2 Remove old workflow files

Delete any release workflows inside Guillemot and Uguisu; the single workflow at repo root replaces them.

---

## Phase 5: Rename ImmoCommon → Immogen

### 5.1 Rename GitHub repository

1. Go to **ImmoCommon** → **Settings** → **General**
2. Under "Repository name", change `ImmoCommon` to **Immogen**
3. Click **Rename**

### 5.2 Update local clone remote

```bash
git remote set-url origin https://github.com/LPFchan/Immogen.git
```

### 5.3 Update README and docs

- Replace "ImmoCommon" with "Immogen" in:
  - `README.md` (root)
  - `lib/` README if present
  - Any `Guillemot/` or `Uguisu/` docs that reference the repo
- Update GitHub URLs to `LPFchan/Immogen`
- Optionally add a note: "Immogen (formerly ImmoCommon) — shared library for the BLE immobilizer system"

### 5.4 Commit and push

```bash
git add README.md
git add -A
git commit -m "Rename project to Immogen"
git push origin main
```

---

## Phase 6: Update Whimbrel

Whimbrel fetches releases from GitHub. After migration, use the **Immogen** repo and expect one release with four assets.

### 6.1 Update release API base

In `Whimbrel/js/api.js` (or equivalent), change:

- From: separate fetch to `LPFchan/Guillemot` and `LPFchan/Uguisu`
- To: single fetch to `LPFchan/Immogen`

The existing filter `r.assets.some((a) => a.name.endsWith(".zip"))` still works, since each release will have multiple `.zip` assets.

### 6.2 Update asset selection

Releases now contain both device types per release:
- `guillemot-{version}.zip` and `guillemot-{version}.hex`
- `uguisu-{version}.zip` and `uguisu-{version}.hex`

**Important:** The current `releaseData.assets.find((a) => a.name.endsWith(".zip"))` returns the first `.zip` asset, which may not match the selected device. Update logic to:

1. Fetch the latest release from Immogen (once per device selection).
2. Filter assets by device type: fob → `uguisu-*.zip`, receiver → `guillemot-*.zip`.

Example:

```javascript
const prefix = fwSelectedDeviceName === "Guillemot" ? "guillemot" : "uguisu";
const zipAsset = releaseData.assets.find((a) => a.name.startsWith(prefix) && a.name.endsWith(".zip"));
```

### 6.3 Update config

In `Whimbrel/js/config.js`, add `GITHUB_REPO` and set it to `Immogen`:

```javascript
GITHUB_OWNER: "LPFchan",
GITHUB_REPO: "Immogen",
```

Update `fetchReleases` / `fetchDeviceReleases` to use `CONFIG.GITHUB_REPO` instead of hardcoded `"Guillemot"` or `"Uguisu"` when fetching from Immogen.

---

## Phase 7: Verify and Push

### 7.1 Build both firmwares locally

```bash
cd Guillemot/firmware
pio run
cd ../..

cd Uguisu/firmware
pio run
cd ../..
```

### 7.2 Verify history

```bash
git log --oneline -20
git log --oneline Guillemot/
git log --oneline Uguisu/
```

### 7.3 Push to remote

```bash
git push origin main
```

---

## Phase 8: Post-Migration Cleanup

1. **Archive Guillemot and Uguisu repos**
   - Settings → Danger Zone → Archive repository
   - Add README notice: "Merged into [Immogen](https://github.com/LPFchan/Immogen)."

2. **Update external links**
   - Docs, badges, or references to ImmoCommon or old repo URLs

3. **Release workflow setup**
   - Ensure `RELEASE_TOKEN` (or equivalent) secret exists if required for releases

---

## Release usage

After migration, tag and push to trigger a release:

```bash
git tag v1.0.0
git push origin v1.0.0
```

This builds both firmwares and creates a release with:
- `guillemot-1.0.0.hex`
- `guillemot-1.0.0.zip`
- `uguisu-1.0.0.hex`
- `uguisu-1.0.0.zip`

---

## Checklist

- [x] Phase 1: Restructure ImmoCommon (src → lib/)
- [x] Phase 2: subtree add Guillemot, flatten firmware, remove submodule, fix platformio.ini, delete .gitmodules
- [x] Phase 3: subtree add Uguisu, flatten firmware, remove submodule, consolidate tools to /tools, fix platformio.ini, delete .gitmodules
- [x] Phase 4: Single CI workflow (both firmwares, renamed artifacts)
- [x] Phase 5: Rename repo to Immogen, update docs
- [x] Phase 6: Update Whimbrel (Immogen, GITHUB_REPO, asset selection by device type)
- [ ] Phase 7: Build both, verify history, push
- [ ] Phase 8: Archive old repos, update links
