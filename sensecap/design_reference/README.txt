# SenseCAP D1 UI reference pack

Seven exact 480 × 480 pixel layout references, intended for native LVGL implementation.

01_home.png — Home overview and quick controls
02_lights.png — Six relay controls
03_fans.png — Fan gauge, presets and mode
04_climate.png — Temperature, humidity, CO₂ and radar presence
05_energy.png — Voltage/current/power and energy history
06_automation.png — Four controller presets and master toggle
07_settings.png — Network, controller, display and diagnostics
00_all_screens.png — Reference overview

Implementation constraints:
- Reproduce exact layout hierarchy, sizing, spacing, contrast, icons, and states using LVGL native widgets.
- The mockup values are illustrative, not live telemetry. Bind actual controller state.
- Four primary navigation tabs: Home, Devices, Insights, More. Lights/Fans are under Devices; Climate/Energy under Insights; Automation/Settings under More.
- No background photos, no webview; use embedded font and simple vector/bitmap icons.
- Preserve existing networking and control protocol, and fix the offline-online oscillation independently.
- Do not use screenshots as runtime backgrounds; create real interactive UI widgets.
- All images 480 × 480 except overview board.

The concepts/ folder contains higher-resolution visual moodboards generated separately. Use the seven 480x480 images for actual screen dimensions, and the concepts as inspiration for polishing icons, lighting, and visual hierarchy. Avoid copying high-cost glow effects literally in LVGL.
