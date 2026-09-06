# ESP32-C6 Room Controller - Project Changes & Updates Log

This document provides a summary of all recent modifications, feature enhancements, branding integrations, and OTA procedures performed on the ESP32-C6 Room Controller project.

---

## 1. UI & Controls Streamlining (Lights & Power Tab)
**File Modified:** `data/index.html`

- **Removed Redundant Push Buttons:**
  - In the **Settings -> Lights & Power** tab, each indicator and logging row previously contained both a text button (`btn-status-toggle`) and a toggle switch (`cyber-switch-sub`).
  - Removed the redundant buttons for:
    - **Radar Status Light** (`#btnToggleRadarStatus`)
    - **Radar Detection Light** (`#btnToggleRadarOutput`)
    - **Controller Status Light** (`#btnToggleControllerLed`)
    - **Enable Power Consumption Logging** (`#btnTogglePowerLog`)
  - Kept the clean cyberpunk toggle switches (`cyber-switch-sub`) which directly trigger their respective JavaScript and REST / WebSocket handlers.

---

## 2. Branding & Media Integration (`techpanda.webp`)
**Files Added/Modified:**
- `data/techpanda.webp`
- `data/index.html`
- `data/style.css`

### Changes Made:
1. **Asset Deployment into LittleFS:**
   - Copied `techpanda.webp` into the `data/` directory so it is bundled with LittleFS and served directly at `/techpanda.webp`.
2. **Dashboard Favicon:**
   - Added favicon declarations in `<head>`:
     ```html
     <link rel="icon" type="image/webp" href="/techpanda.webp">
     <link rel="shortcut icon" href="/techpanda.webp">
     ```
3. **Top Navigation Header Brand Logo:**
   - Replaced generic SVG lightning icon inside `.header-brand-logo` with `<img src="/techpanda.webp" alt="Tech Panda" class="brand-logo-img">`.
   - Added styles for `.brand-logo-img` with `object-fit: cover` and matching cyber glow border styling.
4. **Settings -> System Info Tab Creator Card:**
   - Added a dedicated **Tech Panda Official Channel** card with:
     - Circular glowing avatar (`author-avatar-img`).
     - Channel badge, title, and description.
     - Styled red cyberpunk button linking to `https://www.youtube.com/@TechPanda-4k`.
     - Full mobile responsive rules and micro-animations.

---

## 3. OTA Upload Guide (Windows PowerShell Specifics)

### Why `curl -F ...` Failed in PowerShell
In Windows PowerShell, `curl` is a built-in alias for `Invoke-WebRequest`, which does not recognize `-F` (multipart/form-data). To run actual curl, you must run **`curl.exe`**.

### Commands to Build & Flash via OTA:

#### Step 1: Build the Binary Files
```powershell
# Build firmware binary (.bin)
pio run

# Build LittleFS filesystem image (.bin)
pio run -t buildfs
```

#### Step 2: Upload Using `curl.exe` in PowerShell
Replace `192.168.100.232` with your controller's IP address:

* **Upload Firmware:**
  ```powershell
  curl.exe -F "update=@.pio/build/esp32-c6-wroom1/firmware.bin" -F "target=firmware" http://192.168.100.232/api/ota/upload
  ```

* **Upload LittleFS Filesystem (Static Web Files & Images):**
  ```powershell
  curl.exe -F "update=@.pio/build/esp32-c6-wroom1/littlefs.bin" -F "target=filesystem" http://192.168.100.232/api/ota/upload
  ```

#### Alternative: Web GUI OTA Upload
You can also upload directly through the web UI:
1. Open `http://192.168.100.232/`
2. Go to **Settings -> OTA Update**
3. Select **Firmware** or **Filesystem** and browse for the `.bin` in `.pio\build\esp32-c6-wroom1\`
4. Click **Upload & Flash**.

---

## 4. Mobile & Tablet Responsive Architecture Optimization
**Files Modified:**
- `data/style.css`
- `data/index.html`

### What Was Done:
- **Zero Disturbance to Desktop:** Desktop screen layout (`> 1024px`) remains 100% untouched with identical 3-column relay grids, dual fan speedometer cards, 4-column power telemetry, and 5-tab settings modal.
- **Tablet Optimization (`max-width: 1024px` & `768px`):**
  - Relay grid dynamically reflows to 2 columns.
  - Dual fan cards stack with comfortable margins and responsive tachometer gauge scaling.
  - AC Power & Environmental 4-column cards smoothly reflow into balanced 2x2 grids.
  - Settings modal tabs turn into a horizontal touch-swipe bar with pills.
- **Mobile & Smartphone Optimization (`max-width: 540px`):**
  - Header reorganizes into a compact 2-tier cyber bar with the logo and live metrics aligned.
  - "All ON" and "All OFF" buttons expand to full-width touch targets.
  - Relays and AC power telemetry adapt to 1-column cards for readable text and effortless finger tapping.
  - Settings modal displays as a sleek bottom sheet with smooth scrolling tabs.
  - Quick fan speed presets (OFF, 25%, 50%, 75%, MAX) fit 5-across cleanly without overlapping text.

---

## 5. Mobile Header Alignment Fix (Settings Button in Front of Room Controller)
**Files Modified:**
- `data/index.html`
- `data/style.css`

### What Was Done:
- In the mobile view (`< 540px` and `< 768px`), the Settings button was dropping below into an isolated third row.
- Restructured `.header-brand` with `.header-brand-info` (holding the logo avatar and `Room Controller` text) and placed the `.header-settings-btn` inside the brand row directly opposite / in front of the `Room Controller` title.
- On mobile, `.header-brand` utilizes `justify-content: space-between`, placing `[Logo + Room Controller]` on the left and `[⚙ Settings Button]` directly opposite it on the top line.
- The secondary metrics (`Connected`, `Up: X`, `WiFi: -XX dBm`) sit cleanly on the bottom row without breaking into an awkward third line.
- On desktop (`> 768px`), the layout retains its clean desktop look with the brand on the left and settings button on the right.

---

## 6. Header Alignment Rectification (Left: Brand & Title | Right: Settings Button)
**Files Modified:**
- `data/style.css`
- `data/index.html`

### What Was Done:
- In `data/style.css`, `.header-settings-btn` previously had `order: -1` in compact mobile view, which accidentally pulled the button to the left side in front of the logo.
- Corrected the flexbox ordering:
  - `.header-brand-info` (`[Tech Panda Avatar + Room Controller Title]`): Set to `order: 1` aligned on the **left**.
  - `.header-settings-btn` (`[⚙ Settings Button]`): Set to `order: 2` with `margin-left: auto` anchored to the far **right**.
- Applicable across all breakpoints (`max-width: 540px`, `max-width: 768px`, and desktop).
