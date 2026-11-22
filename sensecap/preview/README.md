# Room Panel V3 native preview

The Windows preview builds the production Ui.cpp against the exact LVGL and ArduinoJson libraries installed by the D1 PlatformIO environment. It substitutes desktop adapters for display, input, preferences, and controller state. The 480 x 480 RGB565 framebuffer is rendered by LVGL. Fixture data is synthetic and control actions are never sent to a device.

After building the D1 dependency environment, run these commands from the `sensecap` project folder:

```powershell
python -m pip install ziglang==0.13.0 Pillow
python preview/build.py
python preview/capture.py
& .pio/preview/room-panel.exe
```

The executable opens a native mouse-driven window. The capture script generates eight screens and three contact sheets under screenshots. Fixtures cover connected, disconnected with long names, and pending with long names and elevated climate readings. The screenshots are initial viewports. Settings remains scrollable and the preview does not exercise physical touch, command success, or hardware timings.
