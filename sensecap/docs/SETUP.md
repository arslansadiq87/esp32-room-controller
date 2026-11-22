# Flash and pair

1. Identify the SenseCAP's **ESP32-S3** serial port in Device Manager or with `pio device list`. The room controller has its own port. On the SenseCAP, use the ESP32 programming mode documented by Seeed if automatic download mode fails. See [Seeed flashing instructions](https://wiki.seeedstudio.com/SenseCAP_Indicator_How_To_Flash_The_Default_Firmware/).
2. Build and flash the optional standalone bring-up image. Replace `COMx` with the D1 port:

   ```powershell
   pio run -d sensecap -e d1-bringup
   pio run -d sensecap -e d1-bringup -t upload --upload-port COMx
   pio device monitor -d sensecap -e d1-bringup --port COMx
   ```

   Touch all four corners, confirm the corner name and bright/dim response, and confirm PSRAM in the monitor. Close the monitor before flashing the dashboard.
3. Build and flash the dashboard:

   ```powershell
   pio run -d sensecap -e d1
   pio run -d sensecap -e d1 -t upload --upload-port COMx
   ```

4. On the panel, scan for Wi-Fi, select an SSID, and enter its password on the on-screen keyboard. You can type a hidden network name. Use the controller hostname or enter its IP, such as `192.168.100.227` on the current LAN. Touch **Test and save connection**. The panel opens Home after pairing. When changing only the target address, you can leave the password blank to reuse the saved password for the same SSID.
5. For direct AP operation, choose **Controller AP**, enter the AP password already configured for your controller, and pair. The shortcut sets the AP name and its 192.168.4.1 address. Core controls work on the LAN without internet.
6. Open **More → Settings** to retry the saved connection, view diagnostics, change pairing, set the local clock, or adjust brightness and sleep. Restore panel settings clears the panel configuration after confirmation. Home, Devices, Insights, and More are the four primary tabs; the detailed pages are subpages within those destinations.

If mDNS does not resolve across your network, use the controller's numeric private IPv4 address. Flash only the ESP32-S3. The D1 project does not contain an RP2040 image.
