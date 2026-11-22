# Controller protocol

Target defaults to `room-controller.local`. The direct controller AP target is `192.168.4.1`. Pairing uses `GET /api/status`, and the working connection uses `ws://<target>/ws`.

## WebSocket

On connection, the controller sends `full_status`. The panel also sends:

```json
{"type":"get_status"}
```

A snapshot has six `relays` with `id/state/sw` and two `dimmers` with `id/level/state/fan_relay/sw`. It also contains nested `names`, `environment`, `presence`, `power`, `system`, and `mqtt`. The server broadcasts `relay_update` and `dimmer_update` for changes, and flat `telemetry` for periodic updates. Radar speed and physical switch levels are refreshed by snapshots. Live power uses `pwr_online` and `power_logging` instead of nested `online` and `logging`. Other commonly used live fields include `env_online`, `presence_state`, `presence_online`, `ac_state`, and `ac_present`. The panel suppresses negative RMS sentinel readings.

```json
{"type":"relay","id":1,"state":true}
{"type":"dimmer","id":2,"level":50,"state":true}
{"type":"dimmer","id":2,"state":false}
```

The controller applies fan level before the state. Off omits level to avoid an unnecessary intermediate operation. The controller's reported fan level is a target, and the fan relay is the controller's reported interlock state. Read-back confirms observed state, since the API has no command identifier, electrical feedback for relays, or acknowledgement of causality.

## HTTP

| Path under /api | Use |
| --- | --- |
| status | Verify pairing and controller identification |
| names | Read names, patch one key such as `{"relay1":"Desk"}`, read back |
| automation | Read and patch field, e.g. `{"relays":[{"id":1,"mode":2}]}` |
| automation/preset | Apply `{"preset":0}` through 3 for Normal, Movie, Cleaning, Away. Read back through automation |
| automation/safety | Read/patch, e.g. `{"t_hyst":0.5}`. GET includes override arrays |
| automation/safety/clear_override | Confirmed POST with `{}` clears all overrides |
| power/history | GET `history` list with `t` timestamps, `w` watts, `v` voltage |
| power/config | Read and write `{"logging_enabled":true}` |
| system/led | Read and write `{"status_led":true}` |
| mmwave/led | Read and write `status_led` and/or `output_led` |

Automation and safety use REST for authoritative reads/writes. Controller `toJson()` now also includes `type: "automation_config"` or `type: "safety_config"` while retaining all existing root fields, so browser dispatch and legacy REST consumers keep their existing payload fields. Patch requests include only changed fields because including a preset in a general automation write invokes preset application. Writes perform a GET, POST, and GET read-back. Successful transport alone does not display success.

The UI omits controller OTA, firmware reboot, Wi-Fi rewriting and energy log deletion. The controller implements all capabilities used by the panel.
