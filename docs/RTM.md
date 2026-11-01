# Requirements Traceability Matrix (RTM)

## 1. Purpose and status

This matrix links each Software Requirements Specification (SRS) requirement to its Software Design Description (SDD) element and one or more planned verification cases.

The test IDs below are planning identifiers, not evidence that tests or firmware have been implemented. All test cases are currently **Planned / Not Run**. Protocol-dependent tests remain blocked until the relevant Alpaca, INDI, or Bluetooth profile decisions are approved. Hardware verification remains blocked until the board, relay module, and switch circuits are selected.

## 2. Traceability conventions

- **SRS ID** refers to the requirement identifier in [SRS.md](SRS.md).
- **SDD reference** identifies the section of [SDD.md](SDD.md) that describes the design element intended to satisfy the requirement.
- **Test ID** refers to the verification case defined in Section 3 of this document.
- **Status** is `Planned` unless a row is explicitly marked `Blocked`; no verification has been run.
- A test may cover multiple requirements, but each requirement is listed in the coverage matrix.

## 3. Planned verification cases

| Test ID | Verification method | Purpose and pass criteria | Prerequisites |
| --- | --- | --- | --- |
| RTM-T-001 | Unit test | Exercise the four open/closed input combinations. Derivation is `OPEN`, `CLOSED`, `UNKNOWN`, and `INVALID` exactly as defined; no unknown or invalid combination is reported as an endpoint. | Position-state implementation or testable model |
| RTM-T-002 | Unit/integration test | Query roof state through each enabled interface and confirm all show the same authoritative position and command state. | Alpaca/INDI/Bluetooth interfaces selected and implemented |
| RTM-T-003 | Unit/integration test | Submit concurrent/conflicting commands through enabled interfaces. No overlapping relay activations occur; selected conflict policy is applied and reported. | Approved conflict policy and interface command sets |
| RTM-T-004 | Unit test | Submit unsupported, ambiguous, or unapproved roof commands. Each is rejected explicitly and the relay remains inactive. | Approved command mapping |
| RTM-T-005 | Hardware inspection/test | Verify the relay contact is compatible with and wired only across the existing manual-button circuit, and is not connected to motor power. | Selected roof circuit and relay; actuator made safe |
| RTM-T-006 | Unit/hardware test | Verify relay inactive output at startup/reset; issue explicit serial bench pulses and confirm bounded closure/release for default and custom durations. Confirm release faults are logged and further pulses are rejected. | Selected board/relay; relay disconnected from roof |
| RTM-T-007 | Unit/integration test | Inject timeout, reset, and communications-loss conditions during/after a command. No automatic retry or replay occurs. | Command-control implementation |
| RTM-T-008 | Protocol interoperability test | Verify supported Alpaca methods/properties, state mapping, command path, and protocol-appropriate errors using compatible clients. | Alpaca service and compatible client available |
| RTM-T-009 | Protocol interoperability test | Verify supported INDI properties/commands, state updates, direct appliance connection, and explicit errors using compatible clients; no external driver/bridge is needed. | INDI service and compatible client available |
| RTM-T-010 | Bluetooth interoperability/security test | Verify approved BLE service/profile, supported operations, access controls, shared state, and shared command validation; verify control is unavailable if not approved. | BLE service and security policy approved |
| RTM-T-011 | Unit/integration test | Inject startup, communication, invalid-input, and relay-control faults. Confirm they are logged/exposed through the selected diagnostic path. Present unavailable state as unavailable/invalid, not as a successful endpoint state. | Diagnostic mechanism and error mapping approved |
| RTM-T-012 | Integration test | Boot with no valid saved Wi-Fi credentials. Verify WPA2 setup AP `ROLLOFFESP`, configuration page at `rolloffesp.local`/`192.168.4.1`, and no relay activation on startup. | Board/network implementation; relay disconnected |
| RTM-T-013 | Web integration test | Submit valid and invalid/missing SSID/password fields. Valid input is accepted for connection; invalid or incomplete input is rejected without replacing the last valid configuration. Confirm the password is not returned or logged. | Provisioning form and credential persistence implementation |
| RTM-T-014 | Integration test | Submit valid home-network credentials. Verify WPA2 association, persistence after successful connection, AP shutdown, `rolloffesp.local`, and configuration web page availability on the home network. | Test access point configured for WPA2 |
| RTM-T-015 | Security inspection/test | Inspect page responses, diagnostics, and logs after provisioning. The home Wi-Fi password is not returned or logged; document the NVS/flash encryption state. | Credential storage and diagnostics implemented |
| RTM-T-016 | Integration/fault-injection test | Make the configured home network unavailable during connection. The appliance reports failure, retries a bounded number of times, then restores the WPA2 setup AP for credential correction. | Board/network implementation |
| RTM-T-017 | Integration test | After successful home Wi-Fi association, verify setup AP is disabled while web configuration remains available on the station interface; verify Wi-Fi credential changes are rejected except when addressed to the setup AP. | AP lifecycle and web server implemented |
| RTM-T-018 | Wi-Fi scan/configuration test | Scan nearby networks and verify the setup access point SSID is exactly `ROLLOFFESP`. | Target board and Wi-Fi implementation |
| RTM-T-019 | Network interoperability test | Resolve `rolloffesp.local` using mDNS on the setup AP and home network; reach the setup page at the specified URL in setup mode. | mDNS implementation; clients/network support mDNS |
| RTM-T-020 | Wi-Fi security test | Verify the initial `ROLLOFFESP` AP uses WPA2 and accepts the specified default password `12345678`; verify access without that credential is denied. | Setup AP implementation |
| RTM-T-021 | Hardware inspection/test | Confirm USB-C powers the selected board and supports code upload; verify relay module VIN supply path/current budget, 5 V requirements, high-/low-trigger selection, 3.3 V GPIO compatibility, reset-safe inactive level, and isolation wiring. | Final board and relay module datasheet |
| RTM-T-022 | Hardware inspection/test | Verify endpoint input circuitry is compatible with selected switch voltage, polarity, and contacts; verify inactive relay behavior while ESP32 is unpowered, held in reset, and before GPIO initialization. | Final switches, input circuit, board, and relay selected |
| RTM-T-023 | Resource/soak test | Measure flash/RAM headroom and exercise concurrent Alpaca, INDI, and approved BLE services; confirm bounded web processing and that slow requests do not delay relay release. | Target implementation and board |
| RTM-T-024 | Security review/configuration test | Verify network and BLE movement commands meet the approved access-control policy; confirm diagnostic/configuration endpoints cannot initiate roof movement. | Access-control policy and interfaces approved |
| RTM-T-025 | Testability review | Demonstrate software and protocol tests can run with relay hardware disconnected or roof drive made safe. | Test harness and implementation |
| RTM-T-026 | Safety/documentation review | Confirm software safety claims do not replace mechanical stops, independent drive protections, or an emergency stop. | Documentation and system design review |
| RTM-T-027 | Supervised hardware integration test | Verify end-to-end operation against the existing roof control circuit only after bench, interface, and safety tests pass; relay emulates the button and position is confirmed only by endpoint inputs. | All hardware and behavior decisions closed; supervised test plan approved |
| RTM-T-028 | Hardware/functional test | Close each position switch independently. Confirm the green open LED illuminates only for the open switch and the red closed LED illuminates only for the closed switch; confirm the corresponding GPIO state. | Selected LEDs, resistors, switches, input circuit, and board |
| RTM-T-029 | Hardware test | Apply USB-C board power and verify the red power LED illuminates; remove power and verify it turns off. Confirm current-limiting resistor/driver and board supply budget. | Selected LED/resistor and board |
| RTM-T-030 | Configuration/integration test | Verify the button-press duration defaults to 1 second, can be changed through the web configuration page, rejects values outside the approved bound, and always releases the relay at its deadline. | Web configuration and relay service implemented; maximum duration approved |
| RTM-T-031 | Startup/interlock test | Boot with neither endpoint active and verify the roof state/direction is unknown, all movement commands are rejected, and the relay remains inactive until an endpoint is confirmed. | Roof control service and safe relay initialization implemented |
| RTM-T-032 | Protocol interoperability test | Verify Alpaca shutter open/close/abort and park/unpark, plus INDI `DOME_SHUTTER`, `DOME_ABORT_MOTION`, and `DOME_PARK`, map to the shared command path; confirm unsupported dome-motion capabilities are not advertised. | Alpaca and INDI services implemented |
| RTM-T-033 | Web configuration test | Verify relay pulse duration displays its current value, accepts only the configured bounded range, persists across reboot, and does not expose a relay activation endpoint. | Wi-Fi web configuration and NVS storage implemented |

## 4. Requirement-to-design-to-test matrix

| SRS requirement ID(s) | SDD design reference(s) | Verification case(s) | Status |
| --- | --- | --- | --- |
| SRS-F-001 | §5.3 Shared roof control service; §6 State model | RTM-T-002 | Planned |
| SRS-F-002–SRS-F-005 | §5.2 Position input service; §6.1 Position state | RTM-T-001 | Planned |
| SRS-F-006 | §6.3 Startup and reset | RTM-T-006, RTM-T-007 | Planned |
| SRS-F-007 | §5.3 Shared roof control service; §7 Concurrency and resource design | RTM-T-003 | Blocked: conflict policy |
| SRS-F-008 | §5.3 Shared roof control service; §5.5–§5.7 protocol adapters | RTM-T-002–RTM-T-004, RTM-T-008–RTM-T-010 | Planned |
| SRS-F-020–SRS-F-021 | §3 System context; §5.4 Relay service | RTM-T-005, RTM-T-027 | Blocked: roof circuit/relay selection |
| SRS-F-022–SRS-F-023 | §5.3 Command processing flow; §5.4 Relay service; §5.8–§5.9 Configuration | RTM-T-006, RTM-T-030, RTM-T-033 | Blocked: relay polarity/max duration |
| SRS-F-024 | §5.4 Relay service; §8 Error handling | RTM-T-006, RTM-T-011 | Blocked: fault observability depends on hardware |
| SRS-F-025–SRS-F-026 | §5.3 Shared roof control service; §8 Error handling | RTM-T-004, RTM-T-031 | Blocked: relay/button behavior verification |
| SRS-F-027 | §5.3 Command processing flow; §6.3 Startup and reset | RTM-T-007 | Planned |
| SRS-F-028 | §5.3 Shared roof control service; §6.3 Startup and reset | RTM-T-031 | Planned |
| SRS-F-040–SRS-F-044 | §5.5 Alpaca adapter | RTM-T-008, RTM-T-032 | Planned |
| SRS-F-060–SRS-F-064 | §5.6 INDI-compatible adapter | RTM-T-009, RTM-T-032 | Blocked: device identity/client validation |
| SRS-F-080 | §5.7 Bluetooth LE adapter; §7 Concurrency and resource design | RTM-T-010, RTM-T-023 | Blocked: BLE profile |
| SRS-F-081–SRS-F-082 | §5.7 Bluetooth LE adapter; §9 Security design | RTM-T-010, RTM-T-024 | Blocked: BLE service/security policy |
| SRS-F-100 | §5.1 Board and hardware abstraction; §6.3 Startup and reset | RTM-T-006, RTM-T-022 | Blocked: hardware selection |
| SRS-F-101–SRS-F-103 | §5.8 Configuration and diagnostics; §8 Error handling | RTM-T-011 | Planned |
| SRS-F-110 | §5.9 Wi-Fi provisioning and configuration web server; §6.3 Startup and reset | RTM-T-012 | Planned |
| SRS-F-111–SRS-F-112 | §5.9 Wi-Fi provisioning and configuration web server | RTM-T-013 | Planned |
| SRS-F-113–SRS-F-114 | §5.9 Wi-Fi provisioning and configuration web server | RTM-T-014 | Planned |
| SRS-F-115 | §5.9 Wi-Fi provisioning and configuration web server; §6.3 Startup and reset; §9 Security design | RTM-T-012, RTM-T-024 | Planned |
| SRS-F-116 | §5.8 Configuration and diagnostics; §5.9 Wi-Fi provisioning and configuration web server; §9 Security design | RTM-T-015 | Planned |
| SRS-F-117 | §5.9 Wi-Fi provisioning and configuration web server; §8 Error handling | RTM-T-016 | Planned |
| SRS-F-118 | §5.9 Wi-Fi provisioning and configuration web server | RTM-T-017 | Planned |
| SRS-F-119 | §5.9 Wi-Fi provisioning and configuration web server | RTM-T-018 | Planned |
| SRS-F-120 | §5.9 Wi-Fi provisioning and configuration web server | RTM-T-019 | Planned |
| SRS-F-121 | §5.9 Wi-Fi provisioning and configuration web server; §9 Security design | RTM-T-020 | Planned |
| SRS-I-001 | §5.1 Board and hardware abstraction | RTM-T-021 | Blocked: board selection |
| SRS-I-002, SRS-I-004–SRS-I-006, SRS-I-011–SRS-I-012 | §5.1 Board and hardware abstraction; §5.4 Relay service | RTM-T-021, RTM-T-022 | Blocked: board/relay selection |
| SRS-I-003 | §5.1 Board and hardware abstraction; §5.2 Position input service | RTM-T-022 | Blocked: switch/input selection |
| SRS-I-007–SRS-I-008 | §5.1 Board and hardware abstraction; §5.2 Position input service | RTM-T-028 | Planned |
| SRS-I-009–SRS-I-010 | §5.1 Board and hardware abstraction | RTM-T-029 | Planned |
| SRS-I-020 | §5.5 Alpaca adapter | RTM-T-008 | Planned |
| SRS-I-021–SRS-I-022 | §5.3 Shared roof control service; §5.6 INDI-compatible adapter; §7 Concurrency and resource design | RTM-T-002, RTM-T-003, RTM-T-009 | Blocked: device identity/conflict policy |
| SRS-I-023 | §5.7 Bluetooth LE adapter | RTM-T-010 | Blocked: BLE profile |
| SRS-I-030 | §5.9 Wi-Fi provisioning and configuration web server | RTM-T-012, RTM-T-013, RTM-T-019 | Planned |
| SRS-I-031 | §5.9 Wi-Fi provisioning and configuration web server | RTM-T-014, RTM-T-033 | Planned |
| SRS-I-032 | §5.9 Wi-Fi provisioning and configuration web server | RTM-T-019 | Planned |
| SRS-NF-001 | §5.1 Board and hardware abstraction; §6.3 Startup and reset | RTM-T-006, RTM-T-022 | Blocked: hardware selection |
| SRS-NF-002 | §5.4 Relay service; §7 Concurrency and resource design | RTM-T-006, RTM-T-023 | Planned |
| SRS-NF-003 | §7 Concurrency and resource design | RTM-T-023 | Blocked: target implementation |
| SRS-NF-004 | §5.3 Shared roof control service; §5.5–§5.7 protocol adapters | RTM-T-002, RTM-T-008–RTM-T-010 | Planned |
| SRS-NF-005 | §9 Security design | RTM-T-024 | Blocked: access-control policy |
| SRS-NF-006 | §10 Verification strategy | RTM-T-025 | Planned |
| SRS-NF-007 | §3 System context; §9 Security design; §10 Verification strategy | RTM-T-026, RTM-T-027 | Planned |
| SRS-NF-008 | §5.9 Wi-Fi provisioning and configuration web server; §9 Security design | RTM-T-020 | Planned |
| SRS-NF-009 | §5.8 Configuration and diagnostics; §5.9 Wi-Fi provisioning and configuration web server; §9 Security design | RTM-T-015 | Planned |
| SRS-NF-010 | §5.9 Wi-Fi provisioning and configuration web server; §7 Concurrency and resource design | RTM-T-023 | Planned |

## 5. Change and execution tracking

When implementation begins:

1. Keep test IDs stable; add IDs for new verification cases rather than reusing retired identifiers.
2. Record test result, date, firmware revision, hardware revision, test environment, and evidence for each executed case.
3. Update status to `Pass`, `Fail`, `Blocked`, or `Not Applicable` with justification.
4. Do not mark an SRS requirement verified solely because its design is documented.
5. If an SRS requirement or SDD design changes, review and update affected matrix rows and tests in the same change.
