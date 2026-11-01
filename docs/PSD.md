# Project Scope Document

## 1. Project overview

Develop a rolloff roof control appliance based on an ESP32 development board with a dual-core 32-bit processor running up to 240 MHz, 4 MB flash, 520 KB SRAM, onboard 2.4 GHz Wi-Fi and Bluetooth 4.2 (LE), and USB code-uploader support. Wi-Fi and Bluetooth are both available as appliance communications options. The board is powered and programmed through its USB-C input. The appliance replaces the roof system's manual button with one relay contact and has two switch inputs that indicate when the roof is open and closed.

The appliance itself hosts both an ASCOM Alpaca interface and an INDI-compatible interface. Both interfaces use the appliance's shared roof-control logic; no external driver or protocol bridge is required. The appliance is an interface to an existing roof drive. It is not, by itself, a motor controller or a complete safety system.

## 2. Goals

- Provide a defined hardware and software interface for one relay output and two position inputs.
- Observe and expose the roof-open and roof-closed switch states.
- Expose roof control and position through an Alpaca interface served by the appliance.
- Expose roof control and position directly to INDI clients through an INDI-compatible interface hosted by the appliance.
- Operate as a self-contained appliance without requiring an external driver or Alpaca-to-INDI bridge.
- Control the relay only according to a documented, validated command and timing model.
- Prevent ambiguous sensor readings or startup conditions from being treated as proof that movement is safe.
- Produce a design that can be tested independently before connection to the roof drive.

## 3. In-scope requirements

### Hardware

1. Use the Freenove ESP32 WROOM v1.3 development board, based on the specified ESP32: dual-core 32-bit processor up to 240 MHz, 4 MB flash, 520 KB SRAM, onboard 2.4 GHz Wi-Fi and Bluetooth 4.2 (LE), and USB code-uploader support.
2. Power the development board through its USB-C input.
3. Support Wi-Fi and Bluetooth as communications options; define the Bluetooth service/protocol and supported operations before implementation.
4. Provide one relay-module control output wired to emulate a press of the roof system's existing manual button.
5. Provide one input for the roof-open switch and one input for the roof-closed switch.
6. Document appropriate electrical isolation, voltage compatibility, input conditioning, and wiring requirements once the exact modules and switches are selected.
7. Verify that the USB-C supply and board power arrangement can support the controller and relay module; do not assume a GPIO pin can supply relay-coil current.
8. Use a single-channel 5 V relay module with optocoupler isolation and selectable high-/low-level trigger; configure and verify the trigger and preserve the module's isolation barrier.
9. Ensure the relay module and its contacts are compatible with the manual button circuit being replaced. The relay is not specified to switch motor power.
10. Provide a green LED that illuminates when the roof-open limit switch contact is closed.
11. Provide a red LED that illuminates when the roof-closed limit switch contact is closed.
12. Provide a red power LED that illuminates when board USB-derived power is present.

### Device firmware and Alpaca interface

1. Provide the Alpaca interface from the ESP32-based appliance.
2. Map the supported roof-control actions and observed position states to the corresponding Alpaca device behavior.
3. Provide a local configuration web page through a WPA2-protected Wi-Fi setup hotspot named `ROLLOFFESP` with default password `12345678`, reachable at `http://rolloffesp.local`, so the user can enter the home Wi-Fi SSID and password.
4. Connect to the configured home Wi-Fi network using WPA2 security, then make the Alpaca and INDI interfaces available on that network.
5. Define Alpaca endpoint/discovery behavior, error reporting, and protocol-version/conformance expectations before implementation.
6. Ensure network commands use the same validated relay-control and safety rules as any other command source.

### Embedded INDI interface

1. Provide an INDI-compatible network interface hosted directly on the ESP32 appliance.
2. Map supported INDI roof-device properties and actions to the same underlying state and control behavior used by the Alpaca interface.
3. Report communication errors and unsupported or invalid states explicitly; do not present stale values as current state without indicating their validity.
4. Define the supported INDI protocol behavior, connection configuration, and client compatibility expectations before implementation.
5. Do not require an external INDI driver process, an Alpaca-to-INDI bridge, or a separate host computer to translate between the two protocols.

### Firmware behavior

1. Initialize the relay output to a safe, non-actuating state.
2. Read and report both position inputs.
3. Distinguish at least the following observed states:
   - Open indication active
   - Closed indication active
   - Neither indication active (position between endpoints or unknown)
   - Both indications active (contradictory/invalid)
4. Define relay activation, release, and maximum activation duration only after the drive's control interface has been established.
5. Handle reset, startup, and sensor faults without initiating unintended motion.

### Documentation and validation

1. Document the ports, hardware assumptions, safety limitations, and unresolved decisions.
2. Validate input-state handling and safe relay initialization in tests or bench checks.
3. Verify relay-to-drive operation with the roof mechanically secured and supervised before deployment.

## 4. Out of scope

- Switching or driving the roof motor directly.
- Designing the roof's motor drive, braking, mechanical stops, or obstruction detection.
- Providing a general-purpose web interface, mobile application, or additional automation integrations beyond the appliance-hosted Alpaca and INDI interfaces. A limited local configuration web page for Wi-Fi provisioning is in scope.
- Defining weather sensing, observatory interlocks, emergency stop hardware, or unattended-operation policy.
- Selecting exact GPIO pins, relay module, switch model, enclosure, or field-wiring specification before the relevant hardware is chosen.

These items may become separate requirements, but must not be assumed from the current port description.

## 5. Assumptions and decisions required

The following details must be resolved before implementing motion-control firmware:

1. **Manual button behavior:** How long must the existing button be pressed? Does a press start or stop motion, select direction, or perform another action? What does the roof control circuit do when the button is released?
2. **Relay semantics:** What contact-closure duration is required to emulate a button press reliably, and what timing or interlocks are needed to match the existing button safely?
3. **Command source:** What local or external source requests opening and closing?
4. **Wi-Fi provisioning:** What recovery procedure is used, should the default setup-hotspot password be changed or rotated, and how are saved credentials protected and cleared?
5. **Bluetooth behavior:** Which Bluetooth services and operations are required, and how should pairing, authorization, and concurrent use with Wi-Fi be handled?
6. **Alpaca behavior:** Which Alpaca device type/actions and properties are supported, and what protocol version, discovery behavior, authentication/network exposure policy, and error semantics are required?
7. **INDI behavior:** Which INDI device/properties and protocol features are exposed, and which INDI clients and connection configuration must be supported?
8. **Electrical interfaces:** What are the relay coil input voltage/current and contact ratings? What voltage, polarity, and contact arrangement are used by each limit switch?
9. **Signal behavior:** Are switch inputs normally open or normally closed? How should debounce, broken wires, and shorted wires be detected or handled?
10. **Operating policy:** What should happen when neither or both position switches are active, or if an endpoint is not reached within an expected time?
11. **Reset and failure behavior:** What relay state is safe during startup, brownout, watchdog reset, or loss of controller power?

Until these are answered, the implementation must not infer direction or initiate movement from position inputs alone.

## 6. Safety and reliability constraints

- Wire the relay as a replacement for the manual button in the roof control circuit, and preserve the existing drive's independent protections.
- Do not expose ESP32 GPIO pins to field voltages beyond their specified limits; use appropriate isolation and conditioning.
- Use a relay module with suitable coil-drive compatibility and contact ratings.
- Keep the relay de-energized unless a validated command explicitly requires activation.
- Treat contradictory inputs as a fault/invalid state, and treat neither input as unknown or in transit—not as an endpoint.
- Provide a supervised commissioning procedure and verify behavior with the actuator disabled before connecting the drive.
- A software controller and two endpoint switches do not replace mechanical safeguards, an emergency stop, or a hazard assessment.

## 7. Acceptance criteria

The initial appliance design is acceptable when:

- The documented hardware uses the specified ESP32 development board, one relay module, and two position-switch inputs.
- The appliance serves the agreed Alpaca and INDI interfaces directly, with no external driver or protocol bridge required.
- The relay uses the selected high-/low-level trigger mode with a verified 3.3 V-compatible, safe inactive logic level.
- On initial provisioning, the appliance provides a hotspot and local configuration page where the user can enter the home Wi-Fi SSID and password; it then connects to that network using WPA2.
- The initial setup hotspot SSID is `ROLLOFFESP` and its default WPA2 password is `12345678`; the appliance uses the mDNS hostname `rolloffesp.local`.
- Because `12345678` is a well-known default, the setup hotspot is disabled after successful home-network association and is not intended for continuous operation.
- The appliance advertises `rolloffesp.local` through mDNS on the setup hotspot and configured home Wi-Fi network.
- After successful home-network association, the setup hotspot and configuration page are disabled unless the user re-enters configuration mode.
- Setup mode cannot actuate the roof relay, and configuration/diagnostic responses do not disclose the saved Wi-Fi password.
- Alpaca and INDI behavior, shared state/control semantics, connection/error handling, and client compatibility are documented and verified with suitable client software.
- The relay interface is wired as a replacement for the manual button, is clearly separated from roof motor power, and its compatibility with that button circuit is verified.
- Firmware starts with the relay in a non-actuating state.
- Each input can be independently read and the four input combinations are represented distinctly.
- The green open indicator and red closed indicator illuminate only when their respective switch contacts are closed; the red power LED indicates board power.
- Invalid/ambiguous position states do not cause an automatic movement request.
- Manual button behavior has been characterized, and relay behavior—including button-press emulation, contact-closure duration, and reset behavior—is documented and tested against the existing roof control circuit before roof operation.
- Bench and supervised integration checks pass with safeguards in place.

## 8. Deliverables

- Project README, project scope document (PSD), and software requirements specification (SRS).
- Software design description (SDD) for the ESP32 firmware architecture.
- Requirements traceability matrix (RTM) linking SRS requirements to SDD elements and verification cases.
- Local Wi-Fi provisioning web page and setup-hotspot behavior.
- Alpaca interface implementation and protocol mapping documentation.
- Embedded INDI interface implementation and protocol mapping documentation.
- Hardware interface and wiring documentation after component selection.
- Provisional circuit schematic and wiring notes, to be finalized against selected components.
- Firmware and tests for input-state handling and safe relay control.
- Commissioning and verification procedure for the selected roof drive.

## 9. Change control

Changes to relay semantics, direction selection, electrical interfaces, or unattended operation affect safety and must be reviewed as explicit requirements before implementation. New user interfaces or integrations should be treated as scope additions.
