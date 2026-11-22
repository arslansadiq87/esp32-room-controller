# V3 baseline audit

The pre-change firmware is recoverable at `refs/checkpoints/sensecap-v3`. The active branch and index were left in place.

V2 dashboard used 139,528 bytes of static RAM and 1,736,710 bytes of app flash. The C6 used 51,288 bytes of static RAM and 1,380,386 bytes of app flash. Runtime PSRAM and heap usage require serial measurements. The D1 Arduino loop owns LVGL, while the network worker runs on core 0. The worker polls WebSocket, runs synchronous HTTP and drains a four-entry request queue.

The seven reference layouts are represented in the overview board. Source inspection found that Home stacked more content than the initial viewport allows. Its 44 px metric cards left only 20 px after padding for two text lines. Energy placed the chart behind several stacked rows. Dynamic labels and glyph rendering still require inspection at 480 × 480.

The existing controller publishes relay and dimmer events, but panel confirmation accepts a matching full snapshot. The worker schedules its first explicit reconciliation 120 ms after sending. Synchronous REST and the worker polling interval can add delay. Their contribution has yet to be measured physically.

Existing API contracts are indexed in API_PROTOCOL.md. Controller routes, MQTT discovery, storage schemas, and browser dependencies require a dedicated compatibility baseline before controller changes.
