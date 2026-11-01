# Firmware Development Setup

## Project toolchain

Firmware is built with Espressif ESP-IDF for the classic ESP32 target. The application uses provisional relay/switch GPIO assignments; confirm the exact board header and electrical interfaces before wiring.

## Install ESP-IDF on Windows

Recommended workflow:

1. Install Espressif's ESP-IDF Installation Manager (EIM) using the official Windows setup instructions: [ESP-IDF Windows setup](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/windows-setup.html).
2. Use EIM's easy installation to install the latest stable ESP-IDF release and its required tools.
3. Install Visual Studio Code and the recommended Espressif ESP-IDF extension. Open this repository as the workspace.
4. In the extension, select or configure the ESP-IDF installation created by EIM. The extension's setup wizard can configure the tool paths and Python environment.
5. Connect the development board using USB-C and identify its serial/USB-JTAG port in Windows Device Manager. A USB-to-UART driver may be required depending on the exact board.

Espressif also documents an EIM command-line workflow. Download the Windows x64 EIM CLI from the official [EIM releases](https://github.com/espressif/idf-im-ui/releases), extract it, and run it from PowerShell or Command Prompt (not by double-clicking). The documented commands include `eim wizard` for an interactive setup or `eim install -n true` for a non-interactive default installation. Follow the installer output for the activation script and installed ESP-IDF path.

## Build, flash, and monitor

Run these commands from an ESP-IDF PowerShell or Command Prompt with the ESP-IDF environment activated. Replace `COMx` with the board's actual serial port.

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COMx flash
idf.py -p COMx monitor
```

The current firmware initializes GPIO25 for the relay and GPIO34/35 for the limit switches. It does not activate the relay on boot. On first startup it creates the `ROLLOFFESP` WPA2 network and serves the configuration page at `http://192.168.4.1` (or `http://rolloffesp.local`). For initial setup, use the numeric setup address to ensure the browser reaches the access-point interface. Enter home Wi-Fi credentials there; after station connection, the setup AP shuts down and the configuration page remains available on the home network at `http://rolloffesp.local`. The page displays the assigned home-network IP as a fallback if mDNS does not resolve; that address is also printed in the USB serial log. If station association repeatedly fails, the setup AP is restored. The page allows a bounded relay pulse duration change; the default is 1000 ms, with a compile-time maximum.

The USB serial console also accepts `status`, `pulse`, and `pulse <duration_ms>` commands for supervised bench testing.

The web page uses plain HTTP and the default setup password is widely known. Use it only on a trusted local network. Wi-Fi credentials are stored in NVS without flash/NVS encryption enabled by this scaffold.

Before bench testing:

1. Keep the relay contacts disconnected from the roof controller.
2. For a visible contact test, wire a separate low-voltage supply, current-limiting resistor, and LED in series through the relay COM/NO contacts. The contacts are dry: do not connect an LED directly across COM/NO without a supply and resistor. Keep this test circuit isolated from the roof controller.
3. Verify the relay module control-input pinout and connect module VCC to board VIN only after confirming board supply capacity and module wiring.
4. Set the module's H/L selector to match `CONFIG_ROLLOFFESP_RELAY_ACTIVE_HIGH` or `CONFIG_ROLLOFFESP_RELAY_ACTIVE_LOW` in `idf.py menuconfig`.
5. Confirm the selected active level and relay's actual inactive state with the roof disconnected.
6. From `idf.py monitor`, enter `status` or issue a deliberately timed `pulse` command. The relay is released automatically at the end of the pulse; verify the bench LED lights only while COM/NO is closed.
7. Disable the USB serial bench console in menuconfig before connecting the relay to a live roof controller.

To leave the serial monitor, press `Ctrl+]`.

## Initial verification

The tooling check is a successful `idf.py build` for the `esp32` target. After flashing, the serial monitor should report the initialized GPIO mapping and that the relay remains inactive; explicit bench commands are available only from the USB serial console.

## Design and hardware gates before implementing control

- Confirm the exact ESP32 board variant, available GPIO pins, and USB serial interface.
- Verify relay module control-input pinout, active/inactive polarity, 3.3 V input compatibility, 5 V VIN supply/current budget, and isolation topology.
- Confirm each endpoint switch's voltage, polarity, contact type, and required input protection.
- Keep the relay disconnected from the roof circuit during development until supervised bench checks pass. Disable the serial bench console before any live roof connection.
