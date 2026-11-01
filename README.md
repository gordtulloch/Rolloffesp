# Rolloff Roof Control Appliance

A project to build an ESP32-based controller for a rolloff observatory roof. The appliance replaces the roof system's manual button with one relay contact and observes roof position through separate open and closed switches.

## Software interfaces

- The ESP32 firmware hosts an ASCOM Alpaca Dome interface for roof control and position reporting.
- An INDI-compatible Dome interface is served directly to INDI clients.
- A local configuration web server provides first-time Wi-Fi setup.

The Alpaca Dome service listens on TCP port 11111 and supports standard shutter status and roof commands. Alpaca discovery is available over UDP port 32227. The INDI service listens on TCP port 7624 and exposes the `RolloffESP` Dome device with connection, shutter, abort, park, and limit-position properties. Both network interfaces use the same roof state and command validation. On initial setup, the appliance creates a WPA2-protected Wi-Fi hotspot named `ROLLOFFESP` with the default password `12345678`; the user can open `http://rolloffesp.local` to enter the home Wi-Fi SSID and password. The appliance then connects to that network using WPA2 and disables the setup hotspot after successful connection. Because the setup password is a well-known default, the hotspot is intended only for initial provisioning. Broader Alpaca and INDI protocol conformance remains to be verified.

## Hardware

- Freenove ESP32 WROOM v1.3 development board with a dual-core 32-bit processor (up to 240 MHz), 4 MB flash, 520 KB SRAM, onboard 2.4 GHz Wi-Fi and Bluetooth 4.2 (LE), and USB code-uploader support
- Power supplied through the development board's USB-C input
- One 5 V single-channel relay module with optocoupler isolation and selectable high-/low-level trigger
- Roof-open limit switch input
- Roof-closed limit switch input
- Green LED for the open-switch contact state
- Red LED for the closed-switch contact state
- Red LED for board power
- An existing roof control circuit with a manual button that the relay contact can replace

The development board is powered and programmed through its USB-C input. Ensure the USB-C supply and board power arrangement can support the board and relay module; do not assume the relay coil can be powered from a board pin. The appliance may use the board's onboard Wi-Fi and Bluetooth 4.2 (LE) as communications options; the specific Bluetooth service/protocol remains to be defined. The relay contact acts as an electrical press of the manual button in the roof control circuit. It is **not** intended to switch roof motor power directly. Confirm the button circuit's electrical behavior and the compatibility of the relay contacts before wiring.

## Ports

| Port | Purpose |
| --- | --- |
| Relay output | Emulates a press of the roof system's manual button through a contact closure |
| Open input | Reports that the roof has reached its open position |
| Closed input | Reports that the roof has reached its closed position |
| Green LED | Illuminates when the open limit switch contact is closed |
| Red status LED | Illuminates when the closed limit switch contact is closed |
| Red power LED | Indicates USB-derived board power is present |

The relay substitutes for the existing manual button, so direction selection and movement behavior follow the roof system's existing control circuit. Confirm how long the button must be pressed, whether a press starts/stops motion or selects direction, and what the roof drive does when the button is released before specifying firmware behavior.

## Intended behavior

- Read the open and closed position switches.
- Control the relay output according to the final, confirmed command behavior.
- Avoid reporting a definite open or closed position unless the corresponding input indicates it.
- Treat contradictory position inputs (both active) and neither input active as states requiring explicit handling; neither state alone establishes that movement is safe.

The switch electrical interface and some reset/recovery details remain open. The firmware provides a `ROLLOFFESP` WPA2 setup AP, web-based Wi-Fi provisioning, `rolloffesp.local` mDNS, and bounded pulse-duration configuration. Provisional GPIO assignments are documented in [WIRING.md](docs/WIRING.md). See [PSD.md](docs/PSD.md) for boundaries and acceptance criteria, [SRS.md](docs/SRS.md) for firmware requirements, [SDD.md](docs/SDD.md) for the software design, and [RTM.md](docs/RTM.md) for requirement-to-design-to-test traceability.

On first boot, connect to the `ROLLOFFESP` hotspot (default WPA2 password `12345678`) and open `http://192.168.4.1` (recommended) or `http://rolloffesp.local`. After provisioning, the setup hotspot turns off and the configuration page is available on the home Wi-Fi at `http://rolloffesp.local`; the page also displays the assigned home-network IP as a fallback. Use a trusted network; the configuration site uses HTTP, not HTTPS.

## Wiring concept

See the [physical wiring diagram](docs/physical-wiring-diagram.svg), [provisional schematic](docs/rolloffesp-provisional-schematic.svg), and [wire-by-wire schedule](docs/WIRING.md#physical-wiring-schedule). GPIO25 is assigned to relay control, with GPIO34/35 for the open/closed inputs. Confirm the board header labels and relay module's exact wiring/polarity before connecting the roof control circuit.

## Firmware development

Firmware uses Espressif ESP-IDF. See [DEVELOPMENT.md](docs/DEVELOPMENT.md) for Windows toolchain setup, bench commands, and build/flash instructions. GPIO input monitoring, bounded relay bench control, Wi-Fi provisioning, web configuration, and embedded Alpaca and INDI Dome services are implemented.

An Alpaca Dome test client is available at [tools/alpaca_client.py](tools/alpaca_client.py); usage examples and current firmware limitations are documented in [DEVELOPMENT.md](docs/DEVELOPMENT.md#alpaca-command-line-test-client).

## Safety notes

- Wire the relay only as a replacement for the manual button in the roof control circuit. Do not connect the ESP32 or relay module directly to roof motor power. Preserve the existing, appropriately rated motor controller/drive and its safety protections.
- Provide suitable input conditioning and protection for field wiring; do not assume limit switches can be connected directly to ESP32 pins without checking voltage, polarity, and noise conditions.
- Define safe relay behavior during boot, reset, loss of power, and firmware failure. The default must not cause unintended roof motion.
- Verify mechanical stops, independent drive protections, and obstruction/weather procedures in the complete installation.
- Test first with the drive disconnected or otherwise made safe, then verify the complete system under supervised conditions.

## Project status

The repository includes ESP-IDF firmware for GPIO bench control, Wi-Fi/web configuration, and embedded Alpaca and INDI services, along with provisional circuit and physical wiring diagrams. Protocol interoperability, enclosure design, and hardware validation remain to be completed; verify all provisional wiring against the exact board and module before installation.
