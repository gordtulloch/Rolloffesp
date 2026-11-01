# Software Requirements Specification (SRS)

## 1. Purpose

This specification defines software requirements for the ESP32 rolloff-roof control appliance. It is intended to guide implementation and verification of the firmware and its communications interfaces.

The appliance replaces the roof control system's manual button with a relay contact. It reads separate roof-open and roof-closed switches and hosts Alpaca and INDI interfaces on the ESP32. It is a controller interface to the existing roof control circuit, not a motor driver or a complete safety system.

## 2. Scope and system context

The software runs on the Freenove ESP32 WROOM v1.3 development board:

- Dual-core 32-bit processor, up to 240 MHz
- 4 MB flash and 520 KB SRAM
- Onboard 2.4 GHz Wi-Fi and Bluetooth 4.2 (LE)
- USB-C input used for board power and code upload

The software controls one relay module output, reads two position-switch inputs, and indicates position-switch and board-power states using three LEDs. It provides:

- A local configuration web page over a setup Wi-Fi hotspot for initial home-network provisioning.
- An Alpaca roof-device interface over Wi-Fi.
- An INDI-compatible roof-device interface hosted directly on the appliance.
- Bluetooth LE as an additional communications option; its service, operations, and relationship to the Alpaca/INDI interfaces are not yet selected.

No separate driver process, protocol bridge, or host computer is required for the appliance to provide its Alpaca and INDI interfaces. Both interfaces shall use the same internal roof state and command-control logic.

## 3. Definitions

| Term | Definition |
| --- | --- |
| Appliance | ESP32 board, relay module, position inputs, and firmware |
| Roof control circuit | Existing circuit and drive that operates the roof using its manual button |
| Button press | Relay contact closure that electrically emulates operating the manual button |
| Endpoint input | Input from the roof-open or roof-closed switch |
| Unknown position | Position cannot be determined from the endpoint inputs, including neither endpoint being active |
| Invalid position | Contradictory endpoint indications, including both endpoint inputs active |

## 4. Assumptions and open decisions

The following details are intentionally unresolved and must be decided before the associated behavior is implemented:

1. Manual button behavior: press duration, whether a press starts/stops motion or selects a direction, and behavior on release.
2. How Alpaca open/close requests map to the existing button behavior, particularly if one button does not independently select direction.
3. Alpaca device profile, supported actions and properties, protocol version, discovery behavior, and error mapping.
4. INDI device profile, supported properties and commands, protocol behavior, and supported client versions.
5. Bluetooth LE service/profile, supported operations, pairing and authorization policy, and whether it is intended for setup, control, status, or some combination.
6. Setup-hotspot security, access/recovery procedure, credential protection at rest, and how saved credentials are cleared.
7. Relay module wiring, relay active polarity, verification of the assigned GPIOs against the exact board revision, switch electrical characteristics, and input conditioning.
8. Whether requests through the two network interfaces can be accepted concurrently, and how simultaneous/conflicting commands should be resolved.
9. How switch faults, timeouts, resets, loss of communications, and recovery are handled beyond the safe defaults specified below.

Open decisions do not permit firmware to guess a movement command or represent an unknown position as a confirmed endpoint.

## 5. Functional requirements

### 5.1 Shared control and state

| ID | Requirement |
| --- | --- |
| SRS-F-001 | The firmware shall maintain one authoritative internal roof-control state used by all enabled communications interfaces. |
| SRS-F-002 | The firmware shall represent the two endpoint inputs as four distinct combinations: open only, closed only, neither active, and both active. |
| SRS-F-003 | The firmware shall report open only when the open input is active and the closed input is inactive. It shall report closed only when the closed input is active and the open input is inactive. |
| SRS-F-004 | When neither endpoint input is active, the firmware shall represent position as unknown or between endpoints, not as open or closed. |
| SRS-F-005 | When both endpoint inputs are active, the firmware shall represent position as invalid/faulted and shall not treat either endpoint as confirmed. |
| SRS-F-006 | The firmware shall not initiate relay activation automatically as a result of startup, reset, a change in endpoint input, or communication reconnection. |
| SRS-F-007 | The firmware shall serialize or otherwise safely arbitrate commands that could operate the single relay. It shall not issue overlapping relay activations. The conflict policy for simultaneous independent requests shall be defined before implementation. |
| SRS-F-008 | The firmware shall provide a common validation and relay-control path for requests received through Alpaca, INDI, and any Bluetooth control service that is approved for implementation. |

### 5.2 Relay and roof commands

| ID | Requirement |
| --- | --- |
| SRS-F-020 | The relay shall emulate only the electrical action of the existing manual button, using a contact closure compatible with that button circuit. |
| SRS-F-021 | The relay shall not be used by the firmware as a switch for roof motor power. |
| SRS-F-022 | The firmware shall keep the relay in its verified non-actuating state except during an explicitly accepted and validated button-press command. |
| SRS-F-023 | The firmware shall use a bounded, web-configurable contact-closure duration for each button press, defaulting to 1 second. The duration shall not be left indefinitely active. |
| SRS-F-024 | If firmware detects that it cannot command the relay to its non-actuating state, it shall reject new movement commands and report an error through available interfaces. |
| SRS-F-025 | The firmware shall reject commands that cannot be safely mapped to the known behavior of the existing manual button. |
| SRS-F-026 | The firmware shall implement the verified single-button sequence: from a confirmed endpoint, a press starts travel toward the opposite endpoint; a press during known motion stops travel; a subsequent press reverses direction. It shall reject commands when the motion sequence state is unknown. |
| SRS-F-027 | The firmware shall not automatically retry a button press after a timeout, reset, or communication failure unless a separately reviewed requirement explicitly authorizes that behavior. |
| SRS-F-028 | After startup, if neither endpoint is active and motion/direction is not known, the firmware shall report position/motion as unknown and reject movement commands until an endpoint is confirmed. |

### 5.3 Alpaca interface

| ID | Requirement |
| --- | --- |
| SRS-F-040 | The appliance shall host an Alpaca roof-device interface directly on the ESP32 and make it available over Wi-Fi. |
| SRS-F-041 | The Alpaca interface shall implement the standard shutter operations `OpenShutter`, `CloseShutter`, and `AbortSlew`, and expose standard shutter/motion status and capability properties. `Park`/`Unpark` shall map to closed/open roof state; dome azimuth, home, and slaving features shall be reported unsupported. |
| SRS-F-042 | Alpaca requests that could operate the relay shall pass through the shared command validation and relay-control path. |
| SRS-F-043 | Alpaca position and operation responses shall reflect the current shared internal state and shall preserve unknown and invalid states rather than reporting them as confirmed endpoints. |
| SRS-F-044 | The Alpaca interface shall return protocol-appropriate success or error responses for accepted, rejected, unsupported, or failed operations, according to the selected Alpaca profile. |

### 5.4 INDI interface

| ID | Requirement |
| --- | --- |
| SRS-F-060 | The appliance shall host an INDI-compatible roof-device interface directly on the ESP32; it shall not require a separate INDI driver process or Alpaca-to-INDI bridge. |
| SRS-F-061 | The INDI interface shall implement standard roof-shutter `DOME_SHUTTER` open/close commands, `DOME_ABORT_MOTION`, and `DOME_PARK` status/control mapped to closed/open roof state. Azimuth rotation, home, and slaving features shall not be advertised. |
| SRS-F-062 | INDI commands that could operate the relay shall pass through the shared command validation and relay-control path. |
| SRS-F-063 | INDI state updates shall use the shared internal state and preserve unknown and invalid position conditions. |
| SRS-F-064 | The INDI interface shall report connection, command, and device errors using the selected INDI protocol behavior and shall not present stale data as known-current state without indicating its validity. |

### 5.5 Bluetooth LE

| ID | Requirement |
| --- | --- |
| SRS-F-080 | The software architecture shall allow Bluetooth LE communications in addition to Wi-Fi, subject to the selected service/profile and resource validation. |
| SRS-F-081 | No Bluetooth movement-control operation shall be enabled until its service/profile, command set, pairing, and authorization policy are approved. |
| SRS-F-082 | If Bluetooth status or control operations are implemented, they shall use the same shared state and command validation as the Alpaca and INDI interfaces. |

### 5.6 Startup, recovery, and diagnostics

| ID | Requirement |
| --- | --- |
| SRS-F-100 | On startup and after reset, the firmware shall initialize the relay output to the verified non-actuating state before enabling network command handling. |
| SRS-F-101 | The firmware shall expose or log startup, communications, invalid-input, and relay-control faults through an agreed diagnostic mechanism. |
| SRS-F-102 | Loss and restoration of Wi-Fi, Bluetooth, Alpaca, or INDI connectivity shall not by itself activate the relay or replay a prior command. |
| SRS-F-103 | If a communications interface cannot provide reliable current state, it shall report that condition as unavailable or invalid instead of silently substituting an endpoint value. |

### 5.7 Wi-Fi provisioning and configuration

| ID | Requirement |
| --- | --- |
| SRS-F-110 | When no valid home Wi-Fi credentials are configured, the firmware shall start a local Wi-Fi setup hotspot and configuration web server. |
| SRS-F-111 | The configuration web page shall allow a user connected to the setup hotspot to enter a home Wi-Fi SSID and password. |
| SRS-F-112 | The firmware shall validate submitted fields and shall not treat an unsuccessful or incomplete submission as valid network configuration. |
| SRS-F-113 | After valid credentials are submitted, the appliance shall attempt to connect to the configured Wi-Fi network using WPA2 security and shall persist the credentials only after successful association. |
| SRS-F-114 | After successful connection to the configured network, the firmware shall make Alpaca and INDI services available over that network. |
| SRS-F-115 | Setup mode and its web server shall not actuate the relay or replay a previously received movement command. |
| SRS-F-116 | The configuration page and diagnostics shall not return or log the saved Wi-Fi password. |
| SRS-F-117 | If connection to the configured network fails, the firmware shall report the failure through the setup/configuration interface or an agreed diagnostic, shall not report the device as connected, and shall restore the WPA2 setup hotspot after a bounded number of retries. |
| SRS-F-118 | After successful association with the configured home network, the firmware shall disable the setup hotspot but keep the configuration web server available on the home network for bounded configuration changes. If station reconnection fails, the setup hotspot shall be restored for recovery. |
| SRS-F-119 | The Wi-Fi setup hotspot shall use the SSID `ROLLOFFESP`. |
| SRS-F-120 | The appliance shall advertise the mDNS hostname `rolloffesp.local` on its active Wi-Fi interface and serve the setup page at `http://rolloffesp.local` while setup mode is active. |
| SRS-F-121 | The initial Wi-Fi setup hotspot shall use WPA2 security with the default password `12345678`. |

## 6. External interface requirements

### 6.1 Hardware interfaces

| ID | Requirement |
| --- | --- |
| SRS-I-001 | Firmware shall operate on the specified ESP32 development board and use its USB-C input for board power and code upload. |
| SRS-I-002 | The relay output shall interface to the manual-button circuit through relay contacts; GPIO shall control the relay module only through an electrically compatible drive arrangement. |
| SRS-I-003 | Each endpoint switch shall connect through an input circuit compatible with the selected switch voltage, polarity, and contact arrangement. |
| SRS-I-004 | Firmware shall not assume relay-coil current can be supplied by an ESP32 GPIO pin. |
| SRS-I-005 | The relay control circuit shall default to the non-actuating state while the ESP32 is unpowered, in reset, or before firmware configures its output. This behavior shall be verified on the selected hardware. |
| SRS-I-006 | The 1-channel, 5 V opto-isolated relay module shall preserve isolation between ESP32 logic and the relay coil/contact side according to the selected module's schematic; wiring shall not bypass the isolation barrier. |
| SRS-I-007 | The green open-status LED circuit shall illuminate when the roof-open switch contact is closed and shall otherwise remain off. |
| SRS-I-008 | The red closed-status LED circuit shall illuminate when the roof-closed switch contact is closed and shall otherwise remain off. |
| SRS-I-009 | The red power LED shall illuminate when the board's USB-derived power rail is present. |
| SRS-I-010 | Each LED shall have a suitable current-limiting resistor or driver; LEDs shall not be connected directly across a GPIO or supply without current limiting. |
| SRS-I-011 | The relay module's selectable high-/low-level trigger shall be set to match the firmware's active and inactive output levels, including the verified safe inactive level during reset and boot. The module input shall be verified compatible with 3.3 V GPIO. |
| SRS-I-012 | The relay module shall be supplied from the development board's 5 V VIN rail, subject to verification of the board's supply path and available USB-C current budget; module isolation/jumper wiring shall follow its schematic. |

### 6.2 Communications interfaces

| ID | Requirement |
| --- | --- |
| SRS-I-020 | Alpaca shall be available to compatible clients over Wi-Fi using the selected Alpaca device profile. |
| SRS-I-021 | INDI shall be available to compatible clients through the INDI-compatible service hosted by the appliance, using the selected INDI profile. |
| SRS-I-022 | Both network interfaces shall be able to observe the same appliance state; commands from either shall be subject to shared relay arbitration. |
| SRS-I-023 | Bluetooth LE communications shall conform to the service/profile selected in an approved interface definition. |
| SRS-I-030 | The appliance shall provide a Wi-Fi access point and local web configuration interface for initial entry of home network credentials. |
| SRS-I-031 | The appliance shall connect to the configured home Wi-Fi network using WPA2 security with the user-provided SSID and password. |
| SRS-I-032 | The appliance shall provide the hostname `rolloffesp.local` through mDNS on the configured home Wi-Fi network for local device access. |

## 7. Non-functional requirements

| ID | Requirement |
| --- | --- |
| SRS-NF-001 | The firmware shall preserve safe relay initialization regardless of whether Wi-Fi or Bluetooth initialization succeeds. |
| SRS-NF-002 | The firmware shall not block relay deactivation while processing a network request or protocol response. |
| SRS-NF-003 | Resource use for concurrent Alpaca, INDI, and approved Bluetooth services shall be validated on the target board, including RAM and flash headroom. |
| SRS-NF-004 | All interfaces shall expose consistent roof state and command results; protocol-specific state translations shall not change the underlying hardware behavior. |
| SRS-NF-005 | Network and Bluetooth commands that can cause roof movement shall be subject to an approved access-control policy before deployment. |
| SRS-NF-006 | Firmware shall be testable with relay hardware disconnected or the roof drive otherwise made safe. |
| SRS-NF-007 | No software safety behavior shall be represented as a substitute for mechanical stops, independent drive protections, or an emergency stop. |
| SRS-NF-008 | The setup hotspot shall require WPA2 access; documentation shall identify `12345678` as a well-known default and warn that the setup hotspot is intended only for provisioning. |
| SRS-NF-009 | Wi-Fi credentials shall be stored using the selected platform's protected persistence mechanism where available and shall not be exposed through ordinary status or configuration responses. |
| SRS-NF-010 | Web configuration handling shall be bounded and shall not block relay deactivation or the bounded relay timing function. |

## 8. Verification requirements

Each requirement shall be verified by one or more of inspection, unit test, integration test, or supervised hardware test. At minimum, verification shall include:

1. **Input-state tests:** Exercise all four combinations of endpoint inputs and verify the corresponding open, closed, unknown, and invalid states.
2. **Safe startup tests:** Reset and power-cycle the board while observing the relay output; verify no unintended contact closure occurs.
3. **Command-validation tests:** Verify unsupported, ambiguous, conflicting, and unsafe commands are rejected without activating the relay.
4. **Shared-state tests:** Issue supported status requests through Alpaca and INDI and verify both report consistent state.
5. **Command-path tests:** With the roof drive disconnected or made safe, verify each approved protocol command follows the same relay timing and arbitration behavior.
6. **Fault/reconnection tests:** Interrupt and restore communications and verify the relay is not activated and old commands are not replayed.
7. **Protocol interoperability tests:** Test the selected Alpaca profile with compatible clients and the selected INDI profile with compatible clients.
8. **Bluetooth tests:** If Bluetooth operations are implemented, test the approved service, access control, coexistence expectations, and shared command behavior.
9. **Wi-Fi provisioning tests:** With no saved credentials, verify the WPA2-protected `ROLLOFFESP` hotspot using its default password `12345678`, `rolloffesp.local` mDNS resolution and setup page, field validation, credential persistence, WPA2 network association, and that setup activity cannot activate the relay or reveal the home Wi-Fi password. Verify mDNS resolution on the home network after provisioning.
10. **Supervised integration tests:** Only after bench checks pass, verify operation against the existing roof control circuit under supervised conditions.

## 9. Release criteria

A software release intended to control the roof shall not be considered ready until:

- The manual button behavior and safe relay closure duration have been characterized.
- Alpaca and INDI profiles, commands, state mappings, and client compatibility expectations are documented.
- The Bluetooth scope and access-control policy are resolved for any Bluetooth control operation.
- Setup-hotspot access control, credential storage, provisioning, and recovery behavior are verified.
- Requirements with unresolved safety or command semantics are either completed or explicitly disabled.
- Applicable verification tests pass, including safe relay startup and all four endpoint-input combinations.
- Supervised integration confirms that the relay emulates the manual button and does not switch motor power.
