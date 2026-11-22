# V3 architecture status

The C6 owns device state. The panel publishes no independent automation decisions. Arduino loop owns LVGL and the D1 worker handles the controller connection. The UI and worker share a bounded request queue and synchronized state snapshots. The D1 display and touch drivers are unchanged from the baseline.

Remaining architecture work includes a simulator adapter for the production widgets, separating HTTP from WebSocket servicing, and evaluating an isolated encrypted ESP-NOW bench transport.
