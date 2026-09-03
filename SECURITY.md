# Security policy

## Reporting a vulnerability

Please do not open a public issue for a vulnerability or safety-sensitive flaw.
Use GitHub's **Report a vulnerability** action in the repository Security tab to
open a private advisory with the maintainers.

Include the affected commit or firmware version, target board, reproduction
steps, expected impact and any suggested mitigation. Do not include credentials,
private datasets or third-party proprietary SDK files.

## Scope

Security reports are especially useful for:

- malformed bytecode or serial input that corrupts memory or bypasses limits;
- a `STOP`, `HALT` or error path that leaves motors, actuators or a buzzer active;
- persistence behaviour that runs code other than the program explicitly saved;
- unsafe bounds, stack handling or parser behaviour;
- accidental inclusion of secrets or redistributable restricted components.

The default branch is the supported development version. Until tagged releases
exist, include the exact Git commit in every report.

Hardware can move or energize connected devices. Test untrusted changes with
motors lifted, actuators disconnected or otherwise made physically safe.
