# Architecture

## Board

The driver uses a 480 x 480 RGB565 framebuffer. It selects the ST7701 GX initialization when touch answers at 0x48, or the timing-only DX initialization when touch answers at 0x38, as mapped in the Seeed BSP. The Arduino GFX panel refreshes it from PSRAM. LVGL renders partial 32-row RGB565 buffers. A 9-bit software SPI bus clocks the panel initialization data on GPIO 41/48 with CS through PCA9535 port 0 bit 4. The driver preserves unrelated expander bits, including RP2040 reset. GPIO 45 supplies backlight PWM.

| Signal | D1 mapping |
| --- | --- |
| RGB DE, VSYNC, HSYNC, PCLK | 18, 17, 16, 21 |
| R0 to R4 | 4, 3, 2, 1, 0 |
| G0 to G5 | 10, 9, 8, 7, 6, 5 |
| B0 to B4 | 15, 14, 13, 12, 11 |
| I2C SDA, SCL | 39, 40 |
| PCA9535 | 0x20, with Seeed BSP fallback 0x39. P04 LCD CS, P05 LCD reset, P06 touch interrupt, P07 touch reset |
| FT5x06 touch | 0x48 GX or 0x38 DX, polled at register 0x02. GX mirrors both axes for the Arduino reference orientation. DX uses the BSP zero-rotation mapping. |
| LCD active-high backlight | 45 |

Sources: [Seeed Arduino tutorial](https://wiki.seeedstudio.com/SenseCAP_Indicator_ESP32_Arduino/), [Seeed SDK board definition and FT5x06 driver](https://github.com/Seeed-Solution/SenseCAP_Indicator_ESP32/tree/77edb8d2b9a92fc67965c1b2d4a838f0d09a1800), [Seeed-linked GX ST7701 initialization reference](https://github.com/LongDirtyAnimAlf/SenseCap/tree/8003fb320db707f051d8b7d14d5be4d95e0e870c). The selected profile and expander address are printed to serial. Both display profiles require hardware validation.

## Runtime

| Module | Responsibility |
| --- | --- |
| Display.cpp, PanelInit.h | RGB, expander, touch, PWM, LVGL input and flush. Wake consumes the first press so it cannot operate a load. |
| Protocol.h | Typed channel and telemetry state, bounds checks, freshness and connection generation checks. Shared with host fixtures. |
| Controller.cpp | Core 0 worker owns Wi-Fi, mDNS, HTTP, WebSocket, JSON parsing, retries and command reconciliation. |
| Ui.cpp | LVGL widgets and gesture handling on the Arduino loop task. Updates labels and gauges from copied snapshots. |
| Bringup.cpp | D1-only diagnostic environment with touch targets in all corners and backlight changes. |

The UI sends bounded requests through a four-entry FreeRTOS queue. A mutex protects published snapshots and configuration copies. HTTP runs on the network task with connection, read and payload limits. The UI does not call HTTP or WebSocket directly.

Controls enable after a valid six-relay and two-fan snapshot on the active socket. Wi-Fi, WebSocket transport, snapshot/reachability, and telemetry freshness are tracked separately. A missing telemetry packet no longer forcibly closes an otherwise connected socket; telemetry values become stale after 10 seconds, and a new snapshot is requested every 10 seconds. WebSocket reconnection is bounded by the client's reconnect interval and a worker backoff. Heartbeat is 15 seconds with a 3 second pong timeout, independent of 500 ms telemetry. Configuration/name polling is limited to once every 120 seconds. Failed optional REST reads do not set the socket offline. A fan drag produces one debounced command on release. Load commands expire instead of being replayed across connection generations. During command processing, the UI shows Pending and the worker requests state until it matches or times out. Group actions confirm individual stages and report partial completion if required.

The network worker logs a timestamped Wi-Fi/IP/RSSI, socket/snapshot/freshness, receive/malformed-message, reconnect, pending-command, HTTP duration, free-heap and largest-block sample every 10 seconds. Socket open/close logs include session age, last receive age and heap. These diagnostics do not include Wi-Fi passwords. The UI has four fixed primary destinations (Home, Devices, Insights, More); Lights/Fans, Climate/Energy, Automation, Settings, pairing and safety pages remain available as subpages. The compact header keeps clock, Wi-Fi and controller state visible. The layouts use climate-first home, two-column relay tiles, compact climate metric cards, a fan arc, scene cards and fixed bottom navigation.

WebSocket text fragments are assembled into a fixed 12,000-byte buffer. Oversized messages are rejected and counted; complete and fragmented messages use the same bounded JSON parser. LVGL stays on the loop task; the network task updates shared model state only. Text labels and device tile fills change only when the displayed value/state changes. The 120 ms UI tick is not a frame-rate measurement.

Audit finding: prior code used `enableHeartbeat(2000,1200,2)` and force-disconnected the WebSocket when telemetry age reached 3.5 seconds. Since telemetry is nominally 500 ms, either alone could flap under scheduling/network delay; synchronous HTTP configuration/name calls (up to several seconds each) also ran in the same worker that calls `ws.loop()`. The connection symptom cannot be assigned to a single cause without runtime logs from the panel. No LVGL desktop simulator or physical D1 was available for visual/runtime verification.

Pairing validates HTTP status before storing SSID, password and target together in the panel's NVS. Brightness, timeout, reduced motion and meter verification are stored separately on the panel. There are no committed credentials. Power history is loaded on page entry and refreshed at five minute intervals while open, or manually. It uses the timestamps supplied by the controller and displays a trailing window relative to the latest recorded sample. The header clock can be set locally and will need setting again after a full power loss.

## Network boundary

The controller uses unauthenticated LAN HTTP and WebSocket. Use a trusted or isolated network, and keep controller ports out of public proxies and port forwarding. The panel opens sessions only to resolved private IPv4 addresses. Core operation does not need MQTT, Home Assistant, or cloud services. Stronger authentication would require a separate controller and panel protocol change. The existing controller and the RP2040 firmware are independent of this project.
