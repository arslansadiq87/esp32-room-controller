# SenseCAP Indicator D1 Room Panel

The display uses four primary navigation destinationsâ€”Home, Devices, Insights and Moreâ€”with the detailed device, climate/energy and automation/settings pages accessible from each destination.

Native 480 x 480 LVGL touchscreen for the ESP32-C6 Room Controller. The D1 joins the same local network or the controller's own access point. It uses the controller's existing WebSocket and HTTP interfaces.

## Build

Install the **PlatformIO IDE** extension (`platformio.platformio-ide`) in VS Code and open this `sensecap/` folder as a separate project. From the repository root, build with:

```sh
pio run -d sensecap -e d1
pio run -d sensecap -e d1-bringup
```

The two environments pin pioarduino **55.03.39**, Arduino-ESP32 **3.3.9** (ESP-IDF libraries **5.5.4**), Arduino GFX **1.6.9**, LVGL **9.2.2**, ArduinoJson **7.4.3**, and WebSockets **2.7.2**. The target is ESP32-S3, with 8 MB QIO flash and octal PSRAM. The PlatformIO board definition supplies the processor and memory settings. The D1 GPIO mapping lives in the display driver.

## Screens

- **Home** follows the climate-first overview with room readings, occupancy, live power, four quick controls, active relay count, fan speeds, and the active scene.
- **Devices â†’ Lights** shows six named relay tiles, switch state and automation status. Bulk actions require confirmation. **Devices â†’ Fans** has two fan selectors, a circular speed gauge, ON/OFF and five speed presets.
- **Insights â†’ Climate** shows temperature, humidity, COâ‚‚ quality, occupancy, motion, stationary presence, distance, speed and radar signal. **Insights â†’ Energy** shows AC readings, energy totals and a 24-hour history chart with calibration notice.
- **More â†’ Automation** provides four controller presets, the global automation switch, detailed rules and safety controls. **More â†’ Settings** includes display, network/controller diagnostics, device names and pairing.

The UI uses native LVGL widgets with persistent page widgets during telemetry updates and one LVGL owner task. A desktop LVGL simulator is not configured in this repository, so screenshot-to-reference comparison and physical visual verification remain pending.

See [Setup](docs/SETUP.md) for flashing and pairing, [Architecture](docs/ARCHITECTURE.md) for D1 details, [Protocol](docs/API_PROTOCOL.md) for controller integration, and [Testing](docs/TESTING.md) for verification status.


