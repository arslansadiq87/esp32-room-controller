# ESP32-C6-WROOM-1 Smart Home Room Controller Firmware

A modular, non-blocking, production-ready C++ firmware for the **ESP32-C6-WROOM-1** smart home controller using PlatformIO and the Arduino framework.
<p align="center">
  <img src="https://github.com/arslansadiq87/homeautomationv1/blob/main/MCLOGO.png" alt="MCLogo logo" width="180">
  </p>
JLCMC: Quality Automation Parts, Fairly Priced. Find Your Solution Here: https://jlcmc.com?from=MC2607


📺 **[Watch the project video on YouTube](https://youtu.be/7oDmBWbUF9s)**

> 📖 **Looking for the complete feature manual and automation guide?**  
> Read the full [User Manual & Feature Guide (USER_MANUAL.md)](USER_MANUAL.md).

---

## 📌 Hardware Pin Mapping

| Function | Pin / Signal | ESP32-C6 GPIO | Notes / Interface |
| :--- | :--- | :--- | :--- |
| **I2C Bus** | SDA | `GPIO 6` | Shared bus (MCP23017, ACS37800 & STCC4) |
| | SCL | `GPIO 7` | 400 kHz Fast-Mode |
| **CO₂ & Climate Sensor**| STCC4 | `I2C 0x64` | Sensirion True CO₂ (ppm), Temp (°C), Humidity (%RH) |
| **mmWave Presence** | TX / RX | `GPIO 16` / `GPIO 17` | DFRobot Fermion C4002 24GHz Radar (UART0 115200) |
| **I/O Expander** | MCP23017 GPA0–GPA7 | `I2C 0x20` | Wall Switches: `GPA0`=Fan 1, `GPA1`=Fan 2, `GPA2..GPA7`=Relays 1..6 |
| | MCP23017 GPB0–GPB1 | `I2C 0x20` | Fan 1 & Fan 2 Relays (ULN2803A Pins 18 & 17 / OUT1 & OUT2) |
| | MCP23017 GPB2–GPB7 | `I2C 0x20` | 6 Switchable Relays `1` to `6` (ULN2803A Pins 16–11 / OUT3–OUT8) |
| **Power Monitor** | ACS37800 | `I2C 0x60` | Reads $V_{\text{RMS}}$, $I_{\text{RMS}}$, $P_{\text{active}}$, PF |
| **External SPI Flash** | CS / CLK / MOSI / MISO | `19` / `20` / `21` / `22` | W25Q128 (16MB SPI Flash / LittleFS) |
| **AC Dimmer / Fans** | Zero-Cross Detection | `GPIO 5` | Hardware Interrupt (`RISING`) |
| | Triac 1 Gate (MOC3052) | `GPIO 4` | Phase-cut pulse timer (Fan 1 Speed) |
| | Triac 2 Gate (MOC3052) | `GPIO 18` | Phase-cut pulse timer (Fan 2 Speed) |
| **Rotary Encoder 1** | A / B / Pushbutton | `GPIO 0` / `GPIO 1` / `GPIO 23`| Hardware Interrupts; controls Fan 1 |
| **Rotary Encoder 2** | A / B / Pushbutton | `GPIO 2` / `GPIO 3` / `GPIO 10`| Hardware Interrupts; controls Fan 2 |
| **Status LED** | WS2812B Data | `GPIO 11` | FastLED status indicator |
| **USB CDC Console** | D- / D+ | `GPIO 12` / `GPIO 13` | Native USB Serial Debug & Flashing |

---

## 🚀 Features & Architecture

- **True CO₂ & Environmental Monitoring (Sensirion STCC4)**: High-accuracy thermal-conductivity CO₂ concentration (400–5000 ppm), ambient temperature, and relative humidity over I2C.
- **mmWave Human Presence Radar (Fermion C4002)**: 24GHz radar sensor detecting macro-motion, static human presence, breathing / micro-motion, target distance, speed, and signal energy over UART0.
- **AC Phase-Angle Dimming & Relay Interlock**: Zero-cross interrupt on `GPIO 5` combined with high-resolution hardware timers (`esp_timer`) drives MOC3052 optoisolators and BTA16 triacs on `GPIO 4` and `GPIO 18` paired with ULN2803A Pin 18 & 17 isolation relays.
- **I/O Expander (MCP23017)**: 8 physical wall switches on GPA toggle 2 fan circuits (GPA0/GPA1) and 6 general-purpose relays (GPA2..GPA7) driven by a ULN2803A Darlington sink array (Pins 16..11).
- **Rotary Encoders**: Hardware interrupt-based quadrature decoding for smooth dimmer percentage adjustments; pushbuttons toggle load power.
- **Power & Energy Monitoring (ACS37800)**: Real-time telemetry reporting RMS voltage, RMS current, active power, and power factor.
- **Responsive Dark-Mode Dashboard**: Glassmorphic UI stored in LittleFS on external SPI flash (`data/`), communicating with the controller over high-speed WebSockets (`/ws`) for instant two-way state synchronization.
- **Status LED (WS2812B)**:
  - 🔵 **Pulsing Blue / Cyan**: AP Mode / Connecting to Wi-Fi
  - 🟢 **Solid / Breathing Green**: Connected to Wi-Fi & Normal Operation
  - 🔴 **Blinking Red**: Wi-Fi / Sensor Error

---

## 🛠️ Build & Upload Instructions

### 1. Requirements
- [Visual Studio Code](https://code.visualstudio.com/)
- [PlatformIO IDE Extension](https://platformio.org/)

### 2. Set Wi-Fi Credentials Before First Flash

Open [`include/Config.h`](include/Config.h) and find **Network & WiFi Configuration**. Replace these placeholders with your router's Wi-Fi name (SSID) and password, keeping the double quotes:

```cpp
#define DEFAULT_STA_SSID "YourWiFiSSID"
#define DEFAULT_STA_PASS "YourWiFiPassword"
```

Save the file **before building and flashing the firmware**. These values are compiled into the firmware; changing them later requires rebuilding and flashing again. Keep your personal credentials out of commits and shared firmware.

You can also change `DEFAULT_AP_SSID` and `DEFAULT_AP_PASS` in the same section to customize the controller's own access point. Use a password of at least 8 characters and use those credentials when connecting directly to the controller.

Leaving `DEFAULT_STA_SSID` as `"YourWiFiSSID"` starts the controller in standalone AP mode. You can then configure your router connection from the dashboard's Wi-Fi settings. Credentials saved there persist in NVS and take precedence over the defaults in `Config.h`, including after a normal firmware reflash; update them through the dashboard if this board was previously configured.

In `platformio.ini`, also set `upload_port` and `monitor_port` to your board's serial port (currently `COM4`).

### 3. Upload Web UI to Filesystem (LittleFS)
In PlatformIO sidebar or via terminal:
```bash
pio run --target uploadfs
```

### 4. Build & Flash Firmware
```bash
# Build firmware
pio run

# Flash to ESP32-C6
pio run --target upload

# Open Serial Monitor (115200 baud)
pio device monitor
```

### 5. Connect to Dashboard
1. Connect your phone or PC to the Wi-Fi network:
   - **SSID**: `ESP32-RoomController`
   - **Password**: `techpanda123`
   - If you changed `DEFAULT_AP_SSID` or `DEFAULT_AP_PASS`, use your new values instead.
2. Open your web browser and navigate to:
   - `http://192.168.4.1` when connected directly to the controller's access point.
   - When connected to the same router as the controller, use the station IP printed in the Serial Monitor or `http://room-controller.local` if your network supports mDNS.

## 🛠️ Recommended Manufacturing Partner

<a href="https://jlcpcb.com?from=PPOAXWDGOGZXJOSA" target="_blank">
  <img src="https://raw.githubusercontent.com/arslansadiq87/techpanda-inventory/main/PCBlogo.png" alt="JLCPCB Logo" width="220" />
</a>

> **New users receive $123 in coupons.** [Order now with JLCPCB](https://jlcpcb.com?from=PPOAXWDGOGZXJOSA).
>
> JLCPCB provides seamless workflows, transparent pricing, and verified quality for 1–4 layer PCBs – design in EDA, order PCB.

## App Download
