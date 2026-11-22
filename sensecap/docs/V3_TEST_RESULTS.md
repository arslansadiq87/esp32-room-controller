# V3 test results

| Environment | Result | Static RAM | App flash |
| --- | --- | ---: | ---: |
| d1 | Passed | 139,624 B | 1,739,474 B |
| d1-bringup | Passed | 95,164 B | 748,373 B |
| esp32-c6-wroom1 | Passed | 51,288 B | 1,380,386 B |

The D1 build produced application and factory binaries. Host protocol fixtures passed. Full visual acceptance, live integration and hardware soak are pending.

Use the existing SETUP.md instructions for serial upload. For this workstation, set the isolated PlatformIO core first.

```powershell
$env:PLATFORMIO_CORE_DIR = Join-Path $env:LOCALAPPDATA 'PlatformIO\sensecap-v2-verify'
pio run -d sensecap -e d1 -t upload --upload-port COMx
```

Replace COMx with the D1 serial port. This pass performed no uploads.

## Native preview and incremental network changes

The Windows LVGL preview compiled and captured all eight screens in three states. See ../preview/README.md and ../preview/screenshots. Review caught and corrected the clock and status overlap, an unsupported Climate separator glyph, presets initially below the viewport, and long relay names overlapping the status line. The offline long-name capture shows the bounded title. Scroll, dialog, and mouse interaction coverage remains incomplete.

The host protocol fixtures passed again. Event messages with the matching channel and target state can now satisfy pending relay and fan actions. History transport runs in its own FreeRTOS task with a bounded request and result queue. Subsequent verification moved configuration reads and writes to room-http. No physical latency, memory soak, browser, or Home Assistant regression run was performed in this pass.
