# Software Design Description (SDD)

## 1. Purpose

This document describes the planned software architecture for the ESP32 rolloff-roof control appliance. It translates the project scope and software requirements into components, responsibilities, state handling, and interaction flows.

This design includes a provisional GPIO mapping but is not a construction-ready schematic or final protocol specification. Hardware details and protocol behaviors that have not yet been selected are marked as open decisions. Implementation shall not guess unresolved behavior that could cause roof motion.

## 2. Design objectives

The software shall:

- Host Alpaca and INDI-compatible roof interfaces on the ESP32 without a companion driver or protocol bridge.
- Use one shared source of roof state and one shared command-validation and relay-control path.
- Emulate the existing manual button using one relay contact; it shall not switch motor power.
- Represent open, closed, unknown, and invalid input combinations distinctly.
- Start and recover with the relay in its verified non-actuating state.
- Keep protocol processing separate from hardware control so a communications failure cannot directly energize the relay.
- Support Wi-Fi and Bluetooth LE as communication facilities while deferring the Bluetooth service and command scope until approved.

## 3. System context

```text
 Alpaca clients ---------------------- Wi-Fi -------------------+
                                                                |
 INDI clients ------------------------ network -----------------+--> Protocol adapters
                                                                |          |
 Bluetooth LE clients --------------- BLE (if enabled) ----------+
                                                                |
 Setup client ---------------- Wi-Fi setup AP/web page ----------+
                                                                     Shared appliance
 USB-C --> ESP32 power/programming                                  control service
                                                                         |
                                            +----------------------------+------------------+
                                            |                            |                  |
                                      Position inputs               Relay driver        Diagnostics
                                      Open / Closed                     |              and status
                                                                        v
                                                        Existing manual-button circuit
                                                                        |
                                                         Existing roof control/drive
```

The appliance observes endpoint switches and emulates a manual-button press. Direction selection and roof motion behavior remain properties of the existing roof control circuit unless explicitly established otherwise.

The USB-C connection is the board's power and code-upload interface. It is not defined as an operational command interface.

## 4. Architectural overview

The design uses a layered architecture:

1. **Board support and hardware abstraction** — initializes GPIO and board services; provides relay, switch, and LED indicator abstractions.
2. **Roof I/O services** — samples and normalizes endpoint inputs and controls the relay output.
3. **Roof domain/control service** — maintains the authoritative input-derived position, command state, validation, arbitration, and bounded button-press operation.
4. **Protocol adapters** — independently implement Alpaca, INDI-compatible, and any approved Bluetooth LE services. They translate protocol requests and responses to/from the shared domain service.
5. **Wi-Fi network manager and setup web server** — provisions home Wi-Fi credentials through a setup hotspot, manages WPA2 station connection, and serves the local configuration page.
6. **Diagnostics/configuration storage** — provides fault reporting and stores only approved configuration, with safe defaults and validation.

Protocol adapters shall not write GPIO directly. Hardware services shall not interpret Alpaca, INDI, or Bluetooth protocol messages.

## 5. Component design

### 5.1 Board and hardware abstraction

Responsibilities:

- Initialize the selected ESP32 development-board runtime.
- Configure relay output to the verified inactive electrical level before enabling network services.
- Preserve the relay module's optocoupler isolation between logic and relay sides according to its schematic; do not join isolated grounds or bridge isolation jumpers.
- Power the 5 V relay module from the board VIN rail as shown in the schematic, after verifying the board/header power path and USB supply current budget. Follow the relay module's exact power/jumper topology.
- Configure the relay module's high-/low-level trigger selection to match the firmware's asserted and safe inactive GPIO levels; verify 3.3 V input compatibility and boot/reset behavior.
- Configure the two endpoint inputs using the selected electrical interface.
- Provide green open-status and red closed-status LED indicators that illuminate when their corresponding dry-contact switches close.
- Provide a red power LED connected to the USB-derived board power rail through a current-limiting resistor or suitable driver.
- Provide monotonic timing for input sampling and bounded relay activation.
- Keep board-specific pin definitions and active polarities in one hardware configuration module.

The provisional GPIO mapping is GPIO25 for relay output, GPIO34 for the open input, and GPIO35 for the closed input. GPIO34/35 are input-only and use the external pull-ups shown in the schematic. Verify board-header mapping and electrical compatibility before wiring. Relay active/inactive polarity and switch conditioning remain unconfirmed; no field input shall be connected until its voltage and protection requirements are established.

### 5.2 Position input service

Responsibilities:

- Read the roof-open and roof-closed inputs.
- Apply a configurable debounce/filter policy once selected.
- Normalize raw electrical levels into logical `active`/`inactive` values.
- Publish a timestamped snapshot of both normalized input values.
- Derive the position state using the following truth table:

| Open input | Closed input | Derived position |
| --- | --- | --- |
| Active | Inactive | `OPEN` |
| Inactive | Active | `CLOSED` |
| Inactive | Inactive | `UNKNOWN` |
| Active | Active | `INVALID` |

Neither endpoint being active does not distinguish an in-transit roof from an unknown position. Broken-wire detection is only possible if the selected circuit and input design support it.

The provisional design drives the green and red status LEDs through current-limiting resistors from the 3.3 V rail to the respective switch/input node. Closing a switch pulls its input low and completes its LED current path. The power LED indicates the presence of the selected USB-derived board supply. Final resistor values, LED current, polarity, and any additional GPIO-driven alternative must be checked against the selected components.

### 5.3 Shared roof control service

Responsibilities:

- Own the authoritative position snapshot and command status.
- Accept typed commands from approved adapters.
- Validate command support, current operation status, and known button semantics.
- Arbitrate commands so that only one relay activation can be active at a time.
- Request a bounded button press from the relay service and ensure release on completion.
- Reject unsupported or ambiguous commands without actuating the relay.
- Publish state changes and errors for protocol adapters.

The command API expresses roof intent rather than arbitrary GPIO or relay duration controls. Each accepted button press uses the configured duration (default 1 second); the duration is adjustable through the setup web interface and must be bounded and validated.

The standard shutter command profile is:

| Intent | Alpaca Dome | INDI Dome |
| --- | --- | --- |
| Open roof | `OpenShutter` | `DOME_SHUTTER` = `SHUTTER_OPEN` |
| Close roof | `CloseShutter` | `DOME_SHUTTER` = `SHUTTER_CLOSE` |
| Stop known motion | `AbortSlew` | `DOME_ABORT_MOTION` |
| Park/unpark | `Park`/`Unpark`, mapped to closed/open | `DOME_PARK`, mapped to closed/open |

From a confirmed endpoint, the matching open/close command starts movement toward the other endpoint. A press while known to be moving stops the roof; a subsequent press reverses direction. Report motion direction only when it is known from a command issued by the appliance and not invalidated by restart or other uncertainty. If startup finds neither endpoint active, motion/direction is unknown; reject movement commands until an endpoint switch is confirmed. Do not translate ambiguous commands into speculative button presses. Do not advertise azimuth, home, slaving, or dome-rotation features.

The appliance cannot observe a press of the existing physical manual button because it has no feedback input for that button. Manual operation while a remote motion is in progress can desynchronize the firmware's inferred motion state; endpoint switches re-establish a confirmed endpoint state. Until manual-button feedback is added, avoid concurrent manual and remote operation.

#### Command processing flow

1. An adapter parses and validates a protocol request.
2. The adapter submits a typed command to the shared roof control service.
3. The service checks that the command is supported by the approved mapping and that no conflicting relay operation is active.
4. If invalid or ambiguous, the service returns an explicit error and leaves the relay inactive.
5. If accepted, the service requests a button press using the configured duration (default 1 second).
6. The relay service activates the contact, tracks elapsed time, and deactivates it when the duration expires.
7. The service updates command status and notifies the adapters. Physical roof position remains derived from the endpoint inputs rather than assumed from command completion.

No automatic retry or command replay is performed after a timeout, reset, or reconnection.

### 5.4 Relay service

Responsibilities:

- Convert a validated button-press request into relay activation.
- Enforce one bounded activation at a time.
- Deactivate at the configured deadline, and provide an explicit result to the control service.
- Offer a direct safe-release operation used during startup and fault handling.
- Report software-detected relay-control failures.

The relay's inactive polarity and electrically safe state must be measured and verified on the selected module. Software cannot verify physical contact state unless feedback hardware is added; no such feedback is currently specified.

### 5.5 Alpaca adapter

Responsibilities:

- Serve the ASCOM Alpaca Dome shutter subset over Wi-Fi: open, close, abort, state, and capability reporting. Map park/unpark to closed/open roof state.
- Translate supported Alpaca methods/properties into domain queries and typed commands.
- Translate shared position, operation status, and errors into protocol-compliant responses.
- Reject unsupported operations explicitly.

The adapter shall not maintain independent roof-position state. It shall report non-shutter dome capabilities unsupported. See the [ASCOM Alpaca API](https://ascom-standards.org/api/) and [Dome interface definition](https://ascom-standards.org/newdocs/dome.html).

### 5.6 INDI-compatible adapter

The adapter shall expose the INDI Dome roof-shutter controls: `DOME_SHUTTER` open/close, `DOME_ABORT_MOTION`, and `DOME_PARK` mapped to closed/open. It shall not advertise azimuth rotation, home, or slaving capabilities. See the [INDI Dome interface](https://indilib.org/api/classINDI_1_1Dome.html).

Responsibilities:

- Serve the agreed INDI-compatible device interface directly from the ESP32.
- Translate supported INDI properties and commands into shared domain queries and typed commands.
- Publish shared state changes and explicit connection, device, and command errors.
- Avoid reporting a stale cached position as current without communicating its validity.

The exact INDI network transport, server behavior, device profile, properties, and client compatibility targets remain open. This design does not presume a particular INDI library or wire-protocol implementation.

### 5.7 Bluetooth LE adapter

Bluetooth LE is a planned additional communication capability, not yet a defined control protocol.

Responsibilities, if enabled:

- Implement only an approved service/profile and operation set.
- Enforce the approved pairing and authorization policy.
- Route any status or control requests through the same shared domain service.
- Never access GPIO or independently initiate relay operation.

The adapter may be omitted from initial firmware until its service and security requirements are approved. Its omission shall not prevent Wi-Fi Alpaca and INDI operation.

### 5.8 Configuration and diagnostics

Responsibilities:

- Provide validated configuration for Wi-Fi credentials and approved button-press duration.
- Reject invalid configuration and retain a safe non-actuating startup behavior.
- Report startup, input-state, protocol, and relay-control errors using an agreed diagnostic mechanism.
- Keep the saved Wi-Fi password out of page responses and diagnostic output.

### 5.9 Wi-Fi provisioning and configuration web server

Responsibilities:

- On first boot without valid credentials, start a Wi-Fi access point for setup and serve a local configuration page.
- Use `ROLLOFFESP` as the setup access-point SSID and advertise `rolloffesp.local` through mDNS while the setup interface is active.
- Accept the home Wi-Fi SSID and password from a client connected to the setup access point.
- Validate the submitted values, attempt a WPA2-protected station connection, and persist the credentials only after successful association.
- Report connection progress and failure without exposing the saved password.
- On successful station association, make the Alpaca and INDI services available on the home network.
- Advertise `rolloffesp.local` through mDNS on the home network for local appliance access.
- Disable the setup access point after successful home-network association while keeping the configuration web server available on the home network for configuration changes. Restrict Wi-Fi credential changes to requests addressed to the setup AP; restore the setup AP after repeated station connection failures so credentials can be corrected.
- Keep setup operations isolated from roof-control commands; the setup web server shall not call the relay service.
- Ensure web request processing cannot block relay deactivation or its deadline handling.

The setup hotspot uses SSID `ROLLOFFESP` and default WPA2 password `12345678`. This is a well-known, weak default; the setup hotspot shall be enabled only for provisioning and disabled after successful home-network association. The web server remains available on the connected home network for configuration, but changing Wi-Fi credentials is permitted only through the setup AP. If station retries are exhausted, the AP is restored as a recovery path. HTTP is unencrypted, so the configuration page is intended only for the WPA2 setup network or a trusted home LAN; do not enter credentials over an untrusted network. Credentials are persisted in NVS, but flash encryption/NVS encryption is not enabled by the current project configuration. Optional captive-portal behavior, setup re-entry/credential-erasure procedure, and credential encryption at rest remain to be selected. mDNS availability depends on client and network support and shall be tested on both the setup AP and home network.

## 6. State model

### 6.1 Position state

Position is derived from the latest normalized endpoint input snapshot and has one of four values: `OPEN`, `CLOSED`, `UNKNOWN`, or `INVALID`.

The command service shall not infer endpoint arrival from a command request or relay timeout. It shall only report an endpoint when the corresponding single endpoint input is active.

### 6.2 Command state

The implementation should maintain a command-operation state separate from position, with at least:

- `IDLE` — no relay operation is in progress.
- `PRESS_ACTIVE` — relay contact is currently emulating the button press.
- `REJECTED` — latest command was not accepted; associated reason is available to the adapter.
- `FAULT` — a software-detected condition prevents safe command acceptance.

The precise public representation of these states is determined by the selected Alpaca and INDI profiles. Neither protocol adapter may collapse `UNKNOWN` or `INVALID` into `OPEN` or `CLOSED`.

### 6.3 Startup and reset

Startup sequence:

1. Establish the configured inactive output level as early as the platform permits; firmware configuration must match the verified relay-board H/L selector.
2. Initialize input GPIO and sample both endpoint inputs.
3. Initialize the domain service and mark position based on the input truth table.
4. Initialize the Wi-Fi network manager. If no valid home credentials are stored, start setup hotspot mode and the configuration page; otherwise attempt to join the configured network.
5. Initialize the Alpaca HTTP/discovery service with the network manager. Report the Alpaca device disconnected and reject roof commands until the configured home-network station has obtained an IP address. If unprovisioned, the setup interface remains available.
6. Enable roof command handling only after the control service and home-network station are ready; setup mode shall never enable relay commands.

During the current hardware bring-up phase, an explicit USB serial bench console can request a bounded relay pulse. It is not enabled on boot and must be disabled before the relay is connected to a live roof controller. Wi-Fi setup, pulse-duration web configuration, and the Alpaca and INDI Dome services are implemented. Alpaca is available at TCP port 11111, with UDP discovery on port 32227; INDI listens on TCP port 7624. Both interfaces call the shared roof-control module, which owns motion state, endpoint validation, and relay command handling. INDI currently accepts one client connection at a time and publishes limit states through its `ROOF_POSITION` switch vector.

Failure to start Wi-Fi, INDI, or Bluetooth shall not trigger relay activation. Any reset or communications reconnection shall not replay prior commands. The recovery path after a previously configured network becomes unavailable is an open decision; it must not silently expose an unsecured setup access point.

## 7. Concurrency and resource design

Alpaca and INDI may receive requests concurrently; Bluetooth may also be enabled later. All command submissions shall be serialized by the shared control service or a single command queue.

The implementation shall ensure:

- Protocol request handling cannot block relay deactivation.
- The relay deadline is serviced independently of slow network I/O.
- Published state snapshots are internally consistent.
- Command queue capacity and overload behavior are bounded and documented.
- Simultaneous/conflicting command policy is decided before enabling actuation from multiple interfaces.

Task model, RTOS primitives, network stack selection, maximum clients, queue sizing, and timing budgets are implementation decisions. Memory consumption and coexistence of Wi-Fi, INDI, Alpaca, and Bluetooth must be measured on target hardware.

## 8. Error handling

Errors should be classified at the domain boundary and translated by adapters:

- Unsupported command or unapproved command mapping.
- Conflicting or busy command operation.
- Invalid position indication.
- Communications unavailable or request malformed.
- Invalid configuration.
- Relay-control operation failure detected by firmware.

An adapter shall return protocol-appropriate errors and shall not convert an error into a successful-looking state. A communications failure shall not change relay state or cause an automatic retry. Since the current hardware description provides no relay contact feedback, software shall distinguish a command to release from confirmed physical contact release.

## 9. Security design

Network and Bluetooth movement controls and Wi-Fi provisioning are safety-relevant. Before operational deployment, the project shall define:

- Setup hotspot access control and credential distribution.
- Wi-Fi provisioning, saved credential protection, network exposure, and recovery behavior.
- Alpaca/INDI access-control expectations and limitations.
- Bluetooth pairing, bonding, and authorization policy if BLE control is enabled.
- Credential erasure and reset/recovery behavior.

The setup web server shall not provide roof movement commands or access to the relay service. Configuration responses and diagnostics shall not disclose the saved Wi-Fi password. The protocol adapters shall not permit relay operation through undocumented diagnostic or configuration endpoints. Deployment on an untrusted network is not assumed safe.

## 10. Verification strategy

The design shall be verified in layers:

1. Unit-test position truth table and command validation independently of hardware.
2. Unit-test bounded relay timing and command arbitration against a mocked relay.
3. Test safe relay initialization and output polarity on the selected board and relay module with the roof disconnected.
4. Verify Alpaca and INDI adapters expose consistent state and submit commands through the shared service.
5. Exercise invalid commands, concurrent requests, communication loss, reconnection, reset, and protocol initialization failures.
6. If BLE is enabled, verify approved pairing/access controls and shared command handling.
7. Verify setup hotspot access control, first-time provisioning, invalid form handling, credential persistence, WPA2 association, and failure/recovery behavior.
8. Perform supervised integration with the existing roof control circuit only after bench verification.

The relay shall remain physically disconnected from the roof control circuit during early software and protocol tests.

## 11. Requirement traceability

| Design element | Related SRS requirements |
| --- | --- |
| Shared roof control service and position state | SRS-F-001 through SRS-F-008 |
| Relay service and command flow | SRS-F-020 through SRS-F-027 |
| Alpaca adapter | SRS-F-040 through SRS-F-044 |
| INDI-compatible adapter | SRS-F-060 through SRS-F-064 |
| Bluetooth LE adapter | SRS-F-080 through SRS-F-082 |
| Startup, recovery, diagnostics | SRS-F-100 through SRS-F-103 |
| Wi-Fi provisioning and setup server | SRS-F-110 through SRS-F-121 |
| Board and physical I/O | SRS-I-001 through SRS-I-011 |
| Communications interfaces | SRS-I-020 through SRS-I-023 |
| Wi-Fi setup interface and local naming | SRS-I-030 through SRS-I-032 |
| Resource, consistency, security, and testability | SRS-NF-001 through SRS-NF-010 |

## 12. Open design decisions

The following decisions must be recorded before implementation of affected components:

1. Verify the Freenove ESP32 WROOM v1.3 board revision exposes GPIO25, GPIO34, and GPIO35 at the expected header pins.
2. Exact PCB revision of the pictured one-channel relay module, high-/low-trigger selection, 3.3 V input compatibility, 5 V supply/current, isolation topology, and verified inactive electrical state.
3. Open/closed switch types, electrical characteristics, pull configuration, conditioning, and debounce interval.
4. Existing manual button's electrical characteristics and maximum allowed configurable press duration.
5. Alpaca management/discovery behavior, device identity/number, and compatible-client validation.
6. INDI device identity/name and compatible-client validation.
7. Bluetooth LE service, operations, security policy, and whether control is in initial release.
8. Whether the default WPA2 password for the `ROLLOFFESP` setup hotspot must be changed or rotated, AP lifetime, captive-portal behavior, and recovery entry/credential-erasure procedure.
9. Configuration persistence and protection, diagnostic mechanism, task model, and resource budgets.
10. Home Wi-Fi connection timeout/retry behavior and station/hotspot coexistence.
11. Safe response to switch faults, endpoint timeouts, loss of communications, and recovery.

Until these choices are resolved, the software shall preserve the conservative behavior described here: initialize relay inactive, avoid autonomous actuation, reject ambiguous commands, and represent uncertain position honestly.
