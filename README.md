# ESP32 Room Controller

This repository contains two separate PlatformIO firmware projects for one room automation system:

- [`controller/`](controller/) — ESP32-C6 room controller for relays, fan dimmers, sensors, automation, Wi-Fi, and the browser dashboard.
- [`sensecap/`](sensecap/) — ESP32-S3 firmware for the SenseCAP Indicator D1 touchscreen panel. It connects to the controller over the local network.

The controller remains the authority for device and sensor state. Build and flash each project to its own board; never flash one board's firmware to the other.

**Project video:** [Watch the room controller video on YouTube](https://youtu.be/7oDmBWbUF9s)

**PCB fabrication:** [Order boards from JLCPCB](https://jlcpcb.com?from=PPOAXWDGOGZXJOSA)
<p align="center">
  <img src="https://github.com/arslansadiq87/homeautomationv1/blob/main/MCLOGO.png" alt="MCLogo logo" width="180">
  </p>

**JLCMC:** Quality Automation Parts, Fairly Priced. [Find your solution here](https://jlcmc.com?from=MC2607).

## Repository layout

```text
.
├── controller/
│   ├── src/                ESP32-C6 firmware
│   ├── include/            Controller headers and hardware configuration
│   ├── data/               Browser dashboard files uploaded to LittleFS
│   ├── GerberPackages/     PCB fabrication, BOM, schematic, and pick-and-place files
│   ├── platformio.ini
│   ├── README.md
│   ├── USER_MANUAL.md
│   └── PROJECT_CHANGES.md
└── sensecap/
    ├── src/                SenseCAP Indicator D1 firmware
    ├── include/
    ├── docs/               Setup, protocol, architecture, and testing guides
    ├── tests/
    └── platformio.ini
```

The controller PCB printing files are retained in [`controller/GerberPackages/`](controller/GerberPackages/).

## Requirements

- Visual Studio Code with the PlatformIO IDE extension, or PlatformIO Core (`pio`) on the command line.
- An ESP32-C6 controller board and/or SenseCAP Indicator D1, with a USB data cable for each board.
- The correct serial port for the board being built or flashed.
- A local Wi-Fi network for panel pairing, or the controller's access point.

PlatformIO downloads each project's pinned platform and libraries on its first build.

## Build and flash the controller

Open this repository in a terminal. Before the first flash, edit [`controller/include/Config.h`](controller/include/Config.h) and set the router SSID/password, or leave the placeholders to start in access point mode. Do not commit personal Wi-Fi credentials. Saved credentials may take precedence over the compiled defaults.

Replace `COMx` with the ESP32-C6 board's serial port:

```sh
pio run -d controller -e esp32-c6-wroom1
pio run -d controller -e esp32-c6-wroom1 -t upload --upload-port COMx
pio run -d controller -e esp32-c6-wroom1 -t uploadfs --upload-port COMx
pio device monitor -d controller --port COMx --baud 115200
```

`uploadfs` installs or updates the browser dashboard from `controller/data/`. The controller access point defaults to `ESP32-RoomController` with password `techpanda123`; both values can be changed in `controller/include/Config.h`. Connect to it and open `http://192.168.4.1`, or use the controller's IP address on your LAN. For controller features and hardware details, see the [controller README](controller/README.md) and [user manual](controller/USER_MANUAL.md).

## Build and flash the SenseCAP panel

The main panel firmware runs on the SenseCAP Indicator D1's ESP32-S3. Replace `COMy` with the D1 port; do not use the controller's port.

```sh
pio run -d sensecap -e d1
pio run -d sensecap -e d1 -t upload --upload-port COMy
```

An optional bring-up image checks the panel display, touch, and memory:

```sh
pio run -d sensecap -e d1-bringup
pio run -d sensecap -e d1-bringup -t upload --upload-port COMy
pio device monitor -d sensecap -e d1-bringup --port COMy --baud 115200
```

The bring-up image is diagnostic only; use `d1` for normal panel operation. Follow the [SenseCAP setup guide](sensecap/docs/SETUP.md) to pair the panel. Its [README](sensecap/README.md) and [protocol guide](sensecap/docs/API_PROTOCOL.md) describe the interface to the controller.

## Pair the panel with the controller

1. Flash the controller firmware and upload its dashboard filesystem.
2. Power on both boards and connect the D1 to the same Wi-Fi network as the controller. Alternatively, select **Controller AP** in the panel setup and enter the controller access point password.
3. On the panel, choose the network and enter the controller hostname or IP address if needed. The direct controller access point address is `192.168.4.1`.
4. Select **Test and save connection**. If mDNS discovery fails on your network, enter the controller's numeric LAN IP address.

The panel uses the controller's existing HTTP API and WebSocket. Internet access is not required for local operation after the PlatformIO dependencies have been installed.

## Troubleshooting

- **Build or upload uses the wrong board:** run the command with the correct `-d controller` or `-d sensecap` path and select that board's serial port.
- **Upload fails:** close any serial monitor using the port, check the USB data cable, and retry the board's boot/download procedure.
- **Panel cannot pair:** confirm both devices can reach each other on the LAN, or use Controller AP. Try the controller's numeric IP if its hostname does not resolve.
- **Dashboard is missing or stale:** run the controller `uploadfs` command again.
- **Gerber or assembly files:** find fabrication outputs, schematic, BOM, and pick-and-place files under `controller/GerberPackages/`.

See [`sensecap/docs/TESTING.md`](sensecap/docs/TESTING.md) for recorded verification and hardware checks. Firmware builds do not verify physical wiring, mains safety, or behavior on the assembled hardware.
