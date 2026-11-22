# Verification

## Completed builds

Final verification used PlatformIO Core 6.2.0. The shared cache had an incomplete `toolchain-riscv32-esp` package: its metadata pointed at a local file URI, while the package `bin` directory contained binutils but no GCC/G++. PlatformIO repaired the exact configured 14.2.0 package with:

```powershell
pio pkg install --global --tool 'platformio/toolchain-riscv32-esp@14.2.0+20260121' --force
```

The installed `riscv32-esp-elf-g++` reports GCC 14.2.0. The shared cache also had a missing Arduino framework package manifest, so builds ran sequentially with an isolated core at `%LOCALAPPDATA%\PlatformIO\sensecap-v2-verify`. This used the project's existing versions: pioarduino Espressif 32 55.03.39, Arduino ESP32 3.3.9, framework libraries 5.5.4+sha.735507283d, LVGL 9.2.2, GFX 1.6.9, ArduinoJson 7.4.3 and WebSockets 2.7.2. No framework or library version was changed for verification.

| Environment | Result | Static RAM | Application flash | `firmware.bin` | `firmware.factory.bin` |
| --- | --- | --- | --- | --- | --- |
| d1 | Passed | 139,528 B / 327,680 B (42.6%) | 1,736,710 B / 3,342,336 B (52.0%) | 1,737,120 B | 1,802,656 B |
| d1-bringup | Passed | 95,164 B / 327,680 B (29.0%) | 748,373 B / 3,342,336 B (22.4%) | 748,784 B | 814,320 B |
| esp32-c6-wroom1 | Passed | 51,288 B / 327,680 B (15.7%) | 1,380,386 B / 3,342,336 B (41.3%) | 1,452,256 B | 1,517,792 B |

All three builds compiled, linked and generated both the application and factory binary. No board was flashed.

The application includes the runtime PSRAM check and reports the detected panel profile to serial. The build metrics do not include runtime PSRAM or network heap use.

## Host fixtures

`tests/protocol.cpp` uses synthetic snapshots and telemetry shaped like the actual controller messages. It checks full state, external relay/fan updates, sensor availability, malformed and missing fields, freshness, disconnect, command expiration across reconnect, and partial automation/safety read-back. It is excluded from embedded firmware.

From the repository root after PlatformIO installs the D1 libraries, with any C++17 host compiler:

```sh
c++ -std=c++17 -Iinclude -I.pio/libdeps/d1/ArduinoJson/src tests/protocol.cpp -o /tmp/sensecap-protocol
/tmp/sensecap-protocol
```

On Windows with Python and Zig 0.13.0:

```powershell
python -m pip install ziglang==0.13.0
$testExe = Join-Path $env:TEMP 'sensecap-protocol-tests.exe'
python -m ziglang c++ -std=c++17 -I include -I .pio/libdeps/d1/ArduinoJson/src tests/protocol.cpp -o $testExe
& $testExe
```

The read-only Windows smoke check uses the existing controller:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/live-readonly.ps1
```

The C++17 protocol fixture passed in final verification with split text-frame assembly, oversized-frame rejection, full-state parsing, external relay/fan updates, sensor availability, malformed/missing fields, freshness, disconnect, command expiration across reconnect, and partial automation/safety read-back. `git diff --check` also passes. An earlier read-only live check returned HTTP 200 for status, names, automation, safety and history; a later check timed out. Those historical observations do not establish current connectivity or the root cause of reported D1 flapping.

No SDL/LVGL desktop simulator is configured, so reproducible 480×480 simulator screenshots were not generated. Source layouts were checked against the references. Text fit, exact visual matching, touch behavior, frame rate and memory stability still need simulator or device verification.

## Hardware acceptance pending

- Flash the diagnostic image to the D1 and verify its detected PSRAM, stable panel output, touch in each corner and PWM.
- Flash the dashboard, pair, power-cycle, and verify preserved settings. Test the hostname and manual address paths, router loss, and direct AP operation.
- Compare all six relay states with the browser. Toggle each from the panel, switch, and browser. Verify external changes appear on both screens and check group confirmation.
- Exercise both fans at 0, 25, 50, 75, 100 percent, using the gauge and physical encoder. Interrupt a drag and a pending command by disconnecting. Verify reconnection restores feedback without commands being replayed.
- Read and edit a setting in each automation group and safety section. Verify surviving unrelated values, apply each preset, and confirm override clearing.
- Check sensor availability independently, compare telemetry with the original dashboard, and inspect the history chart with logging enabled and disabled.
- Evaluate touch targets, keyboard, wake gesture, screen timeout and RGB stability during scanning and sustained Wi-Fi operation.
- Run a 60-minute connected soak with serial diagnostics enabled. Correlate any badge transition with Wi-Fi state, socket close, telemetry age, HTTP duration, reconnect count and heap/largest-block samples.

No native SDL/LVGL simulator or SDL development toolchain is installed in this environment. The seven 480×480 references were inspected together against the source layouts, including the four-tab Home / Devices / Insights / More navigation, six relay tiles, dual fan gauge and presets, climate/presence, energy chart, automation presets, and settings/diagnostics. Screenshot-level pixel comparison and true LVGL rendering could not be verified without the simulator or D1.

No physical D1 was connected during development. Flashing, exact panel appearance and spacing, text clipping, touch coordinates, screen rotation, PWM brightness, PSRAM behavior and runtime responsiveness need the actual hardware.

## Radar, freshness and clock verification

The C4002 is attached to the C6. For full snapshots, the panel reads the nested presence object. For periodic telemetry it reads the flat presence_online, presence_state, presence_breathing, presence_dist and presence_energy fields. Only a snapshot supplies speed. State 0 is vacant, 1 stationary, 2 moving and 3 both. An unknown state leaves occupancy unknown. Explicit availability takes precedence over retained measurements. The browser's updatePresenceUI renders the target state even when the online flag is false.

Two read-only LAN captures used the existing controller at 192.168.100.227. The first delivered 24 telemetry messages in 12.1 seconds, with a maximum gap of 553 ms. Radar availability changed false, true, false while power and environment stayed online. The second delivered one snapshot and 24 telemetry messages during a history GET. All three sensor categories delivered 25 updates each, with the last at 12,234 ms. The output baseline arrived at 64 ms. The maximum telemetry gap was 521 ms, history returned HTTP 200, and there were zero malformed JSON messages. No control command was sent. These captures measure the LAN client. D1 scheduler and UART timings require hardware logs.

The C6 stops reporting the radar online after five seconds without an accepted result notification. It retains its last measurements. No controller change was made. Compare a D1 serial capture with the C6 UART log during a physical recurrence to distinguish quiet radar notifications from communication loss.

The D1 uses monotonic milliseconds. A 10-second window applies to accepted live messages and separately to each sensor category. The output control baseline and snapshot-only target speed expire after 20 seconds, allowing two normal snapshot periods. Configuration diagnostics allow 150 seconds for the 120-second background refresh. REST work uses the HTTP worker. Session generation guards publication of its results. The wire format has no per-reading sequence number, so sensor updates within a WebSocket session follow delivery order. Firmware logs include category counts, ages, explicit flags, ignored message reasons and maximum WebSocket service gap.

Run the read-only diagnostic on Windows with the controller reachable:

~~~powershell
& ./tests/live-freshness.ps1 -Controller 192.168.100.227 -Seconds 12
~~~

The D1 starts SNTP when Wi-Fi is available. Time & Date defaults to Karachi (PKT-5), supports local NTP hostnames or IPs and saves display preferences in panel-clock NVS. The ESP32 system clock advances after Wi-Fi drops. After a cold boot without a time source, the header shows Time unset. The last sync shown is from this boot. The manual setting accepts local date and time. The timezone choices for London and New York include seasonal transitions.

The protocol and clock fixtures exercise partial payloads, explicit offline, unknown target states, stationary and zero measurements, independent sensor expiration, dropped messages, wraparound, command session guards, clock format and sync scheduling. The simulator uses the Windows system clock and fixture sensor values. Verify real D1 SNTP packets and retries, Wi-Fi reconnect, timezone conversion across DST, NVS persistence after power cycling, and manual clock holdover on hardware. Use the 115200 baud D1 serial output to check category ages, memory and scheduler gaps during history requests and repeated navigation.
