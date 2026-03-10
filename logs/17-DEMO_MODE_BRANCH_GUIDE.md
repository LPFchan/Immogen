# Whimbrel: Syncing Main → Demo

*Date: 2026-03-10 01:53*

Documents how to bring `main` changes into the `demo` branch and re-enable demo flags.

---

## What Demo Mode Does

The `demo` branch runs without real hardware. All serial port operations, provisioning handshakes, and firmware flashing are simulated with timed delays. The UI and flow are otherwise identical to `main`.

Demo mode is activated by `window.WHIMBREL_DEMO = true` in `index.html`, which is read by `config.js` on load.

---

## Step 1: Merge Main

```bash
git checkout demo
git merge -X theirs main --no-edit
```

`-X theirs` accepts main's version for any conflicts, since main is the authoritative source. Demo flags are re-applied on top afterward.

### Verify the CSS was not corrupted

This is not optional. `-X theirs` has silently dropped CSS rules in a previous Whimbrel merge (March 2026): the `.release-item` and `.progress-container` blocks were replaced by a lone `.release-separator` from demo's side of the conflict. The page loaded fine but the firmware dropdown items were unstyled.

```bash
git diff main -- css/style.css
```

The only differences should be the `.demo-badge` block. If anything else is missing from main's side, restore it manually before proceeding.

---

## Step 2: Re-Apply Demo Flags

### `index.html`

Two changes:

1. Add `window.WHIMBREL_DEMO = true` **before** `config.js` loads:
```html
<script>window.WHIMBREL_DEMO = true;</script>
<script src="js/config.js"></script>
```

2. Add the demo badge to the tagline:
```html
<p class="tagline">Companion Web App for <a href="..." target="_blank">Immogen</a> (Guillemot + Uguisu) <span class="demo-badge" id="demo-badge" aria-hidden="true">Demo</span></p>
```

### `js/config.js`

Add `DEMO_MODE` immediately after the namespace declaration, before `CONFIG`:

```javascript
window.Whimbrel = window.Whimbrel || {};
window.Whimbrel.DEMO_MODE = typeof window !== "undefined" && window.WHIMBREL_DEMO === true;

window.Whimbrel.CONFIG = { ... };
```

### `js/serial.js`

In `isSupported()`, short-circuit for demo so the "unsupported browser" warning doesn't appear:

```javascript
window.Whimbrel.isSupported = function() {
  if (window.Whimbrel.DEMO_MODE) return true;
  return "serial" in navigator;
};
```

### `js/api.js`

In `fetchDeviceReleases()`, return a fake release before making any network call. Both asset names must be present so either device tile works:

```javascript
window.Whimbrel.fetchDeviceReleases = async function(repoName) {
  if (window.Whimbrel.DEMO_MODE) {
    return [{ tag_name: "v0.1.0", html_url: "#", assets: [
      { name: "guillemot-v0.1.0.zip", browser_download_url: "#" },
      { name: "uguisu-v0.1.0.zip", browser_download_url: "#" }
    ]}];
  }
  // ... real implementation
};
```

The asset names must match the prefix filter in `firmware.js` (`guillemot-` or `uguisu-`). A mismatch causes the release selector to show no asset and leaves Flash disabled.

### `js/app.js`

Destructure `DEMO_MODE` at the top of the IIFE:

```javascript
const { CONFIG, DEMO_MODE, generateKey, ... } = window.Whimbrel;
```

In `provisionDevice()`, add the bypass immediately after setting `keysProvisioningInProgress = true`:

```javascript
if (DEMO_MODE) {
  try {
    setStatus("Writing key…");
    await abortableDelay(600, () => keysProvisionAborted);
    if (keysProvisionAborted) { setStatus(""); return; }
    setStatus("Waiting for device to boot…");
    await abortableDelay(900, () => keysProvisionAborted);
    if (keysProvisionAborted) { setStatus(""); return; }
    setStatus("Done. Device provisioned and running.");
    if (deviceId === CONFIG.DEVICE_ID_FOB) {
      fobFlashed = true;
      keysProvisioningInPostCircle = true;
      await runTimeout(el.timeoutIndicator, el.progressCircle, 1500, () => keysProvisionAborted);
      keysProvisioningInPostCircle = false;
      if (!keysProvisionAborted) showStep(2);
    }
    if (deviceId === CONFIG.DEVICE_ID_RX) receiverFlashed = true;
    if (fobFlashed && receiverFlashed && currentStepIdx === 2) {
      await showStep(3);
      triggerConfetti();
    }
  } catch (e) {
    if (!keysProvisionAborted) setStatus(e.message || "Demo error", true);
  } finally {
    keysProvisioningInProgress = false;
  }
  return;
}
// ... real implementation follows
```

### `js/firmware.js`

Destructure `DEMO_MODE` at the top:

```javascript
const { CONFIG, DEMO_MODE, fetchDeviceReleases, ... } = window.Whimbrel;
```

In the Flash button click handler, add the bypass immediately after `fwFlashAborted = false`:

```javascript
if (DEMO_MODE) {
  try {
    setFwStatus("Downloading firmware package...", 0);
    await abortableDelay(400, () => fwFlashAborted);
    if (fwFlashAborted) return;
    setFwStatus("Parsing zip file...", 0.1);
    await abortableDelay(300, () => fwFlashAborted);
    if (fwFlashAborted) return;
    setFwStatus("Starting DFU process...", 0.2);
    for (let i = 1; i <= 10; i++) {
      if (fwFlashAborted) return;
      setFwStatus("Writing firmware...", 0.2 + (i / 10) * 0.8);
      await abortableDelay(200, () => fwFlashAborted);
    }
    if (fwFlashAborted) return;
    setFwStatusSuccessWithLink();
    triggerConfetti();
  } catch (e) {
    if (!fwFlashAborted) setFwStatus(`Error: ${e.message}`, null, true);
  } finally {
    fwFlashingInProgress = false;
  }
  return;
}
// ... real implementation follows
```

### `css/style.css`

Add the `.demo-badge` block. In main this doesn't exist, so it belongs only in demo. Place it near the header/tagline styles:

```css
.demo-badge {
  display: inline-block;
  margin-left: 0.5rem;
  padding: 0.15rem 0.5rem;
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.05em;
  color: var(--surface);
  background: var(--success);
  border-radius: 4px;
  vertical-align: middle;
}
```

---

## Step 3: Commit and Push

```bash
git add index.html js/config.js js/serial.js js/api.js js/app.js js/firmware.js css/style.css
git commit -m "Sync main into demo and re-enable demo flags"
git push origin demo
```

If a CSS fix was needed after the merge, commit that separately with a clear message explaining what was restored and why.

---

## Checklist

- [ ] `git checkout demo && git merge -X theirs main --no-edit`
- [ ] `git diff main -- css/style.css` — verify no rules from main are missing
- [ ] `index.html`: `window.WHIMBREL_DEMO = true` before `config.js`; demo badge in tagline
- [ ] `config.js`: `DEMO_MODE` constant
- [ ] `serial.js`: `isSupported()` bypass
- [ ] `api.js`: fake release with both `guillemot-` and `uguisu-` zip assets
- [ ] `app.js`: `DEMO_MODE` destructured; `provisionDevice()` bypass
- [ ] `firmware.js`: `DEMO_MODE` destructured; Flash button bypass
- [ ] `css/style.css`: `.demo-badge` block present
- [ ] Open the firmware dropdown in browser — items should be left-aligned with padding and a separator above the custom options
- [ ] Full flow test: Guillemot → DFU instructions → Flash (should animate and confetti)
- [ ] Full flow test: Keys tab → Generate → Flash Fob → Flash Receiver → All done
- [ ] No Web Serial permission dialog should appear anywhere
- [ ] Commit and push

---

*Last updated: March 2026 (after monorepo sync + CSS corruption fix)*
