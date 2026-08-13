# D-Bus Interface Specification

## 1. Purpose

This document defines the public D-Bus interface exposed by a running
`input-proxy` instance.

It is the authoritative specification for:

- D-Bus service naming;
- Instance Name validation;
- object paths;
- interfaces;
- properties;
- methods;
- signals;
- discovery;
- compatibility requirements.

Architectural motivation, ownership boundaries, runtime lifecycle, and design
rationale belong in `docs/ARCHITECTURE.md`.

## 2. Design goals

The runtime interface is designed to:

- expose runtime observation without exposing implementation details;
- provide deterministic, idempotent runtime control;
- avoid introducing a central manager or registry service;
- use stable, operator-visible Instance Names;
- rely on standard D-Bus facilities where practical;
- remain completely optional for normal proxy operation.

The D-Bus interface augments the runtime. It is never required for evdev-to-
uinput forwarding.

## 3. D-Bus addressing

The runtime interface is exposed on the system D-Bus.

Each running proxy instance owns one well-known service name derived directly
from its validated Instance Name.

```text
Service

net.controlforge.InputProxy1.Instance.<InstanceName>
```

```text
Object

/net/controlforge/InputProxy1/Instance
```

```text
Interface

net.controlforge.InputProxy1.Instance
```

The project controls the `controlforge.net` domain. The corresponding
`net.controlforge` D-Bus namespace is the project's permanent public namespace.

`InputProxy1` identifies major version 1 of the complete public D-Bus API
namespace, including service names and project-specific interface names.
Backward-compatible additions retain this namespace. An incompatible change to
either contract requires a new major-version namespace.

The well-known service name is the stable D-Bus address of the logical proxy
instance.

If a process exits, ownership of that service name disappears. If the same
logical instance is restarted with the same Instance Name, it requests and
reclaims the same well-known service name.

The D-Bus connection's unique bus name is an implementation detail and MUST NOT
be treated as the logical identity of the proxy instance.

Instance enumeration and client discovery behavior are defined separately in
the Discovery section.

## 4. Standard interfaces

The runtime object supports:

- org.freedesktop.DBus.Properties
- org.freedesktop.DBus.Introspectable
- org.freedesktop.DBus.Peer

Project-specific functionality should not duplicate standard D-Bus interfaces.

## 5. Instance Name

The Instance Name uniquely identifies one logical proxy instance.

The Instance Name is supplied through the `--name` command-line option and is
validated before runtime startup.

A valid Instance Name:

- is between 1 and 79 ASCII bytes long;
- begins with an ASCII letter or underscore;
- contains only ASCII letters, digits, underscores, and hyphens;
- is unique among simultaneously running proxy instances.

Instance Names are case-sensitive. Whitespace is not permitted and is not
trimmed. Names are not normalized, escaped, substituted, truncated, or
case-converted. No Instance Names are reserved.

Invalid names MUST be rejected before Instance Name ownership, startup output,
source acquisition, or virtual-device creation begins.

The Instance Name is used consistently for:

- the virtual device name;
- the D-Bus service name;
- future persistent service installation;
- runtime diagnostics and logging.

One validation routine establishes that the supplied value is suitable for every
representation derived from the Instance Name. Each derived representation uses
the validated Instance Name verbatim.

Uniqueness comparisons use the exact, case-sensitive Instance Name. Because
derived identifiers preserve the validated name without transformation, two
syntactically different valid names do not map to the same derived identifier.

## 6. Public properties

The following properties describe one logical proxy instance.

Unless otherwise noted, properties are read-only and are exposed through the
standard `org.freedesktop.DBus.Properties` interface.

Runtime state remains intentionally decomposed into independent properties.
Clients MUST NOT infer a generalized runtime state from combinations of
individual observations.

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| InstanceName | string | Read | Canonical Instance Name of the logical proxy. |
| Source | string | Read | Configured source path. |
| Version | string | Read | Running `input-proxy` version. |
| PID | uint32 | Read | Current process identifier. |
| Paused | boolean | Read | Current pause state. |
| SourceAvailable | boolean | Read | Current source availability. |
| ActivityWhileRunning | boolean | Read | Retriggerable running activity indication. |
| ActivityWhilePaused | boolean | Read | Throttled paused activity indication. |

`InstanceName`, `Source`, `Version`, and `PID` remain constant for the lifetime
of one process.

`Paused`, `SourceAvailable`, `ActivityWhileRunning`, and
`ActivityWhilePaused` may change during normal runtime operation and MUST report
changes through the standard
`org.freedesktop.DBus.Properties.PropertiesChanged` mechanism.

`InstanceName` is the canonical logical identity exposed by the public
interface. A separate `DeviceName` property MUST NOT be exposed because the
virtual device name is derived directly from the same Instance Name.

## 7. Methods

The runtime-control interface provides deterministic, idempotent methods.

Calling `Pause()` while already paused or `Resume()` while already forwarding is
not an error and does not alter the resulting runtime state.

Process lifetime is intentionally outside the scope of this interface.

### Pause()

Requests the paused state.

Requirements:

- Idempotent.
- Safe to call repeatedly.
- Neutralizes active virtual interaction before suppressing forwarding.

### Resume()

Requests forwarding to resume.

Requirements:

- Idempotent.
- Safe to call repeatedly.
- When the source is available, synchronizes the virtual device to current
  physical-source state before changing `Paused` to `false` and resuming
  forwarding.
- When the source is unavailable, changes `Paused` to `false` without treating
  Source Availability as a method failure. No forwarding occurs until a source
  returns and required synchronization succeeds.
- Does not replay activity that occurred while forwarding was suppressed.
- Does not repeat synchronization when the instance is already unpaused.

## 8. Authorization

Public properties and the standard D-Bus interfaces are available to every
client that system-bus policy permits to communicate with the service.

`Pause()` and `Resume()` change runtime state. `input-proxy` MUST authorize each
call using the caller's authenticated Unix credentials. A call is authorized
only when the caller's Unix user identifier:

- is `0`; or
- equals the effective user identifier of the running `input-proxy` process.

If caller credentials cannot be obtained or the caller is not authorized, the
method MUST return `org.freedesktop.DBus.Error.AccessDenied` without changing
proxy state.

Authorization is non-interactive. The methods MUST NOT invoke polkit or request
user confirmation.

System-bus policy may further restrict service-name ownership or message
delivery, but it MUST NOT broaden the service-level authorization rule.

Version 0.3 does not install or modify system-bus policy. An administrator or
development environment must provide policy that permits the intended runtime
and client access. Supported persistent policy installation is Version 0.4
deployment work. Missing or restrictive policy may make D-Bus integration
unavailable, but MUST NOT prevent normal evdev-to-uinput forwarding.

## 9. Activity semantics

### Running activity

Controlled by:

    --activity-timeout-ms

Default:

    5000

Meaning:

- Activity sets `ActivityWhileRunning=true`.
- Each additional activity restarts the timer.
- The property returns to `false` only after the timeout expires without further
  activity.

### Paused activity

Controlled by:

    --detection-throttle-ms

Default:

    250

Meaning:

- Activity sets `ActivityWhilePaused=true`.
- Additional activity during the throttle interval does not extend the interval.
- The property returns to `false` when the throttle expires.
- A later activity may assert it again.

## 10. Discovery

Clients that already know the desired Instance Name SHOULD communicate directly
with:

```text
net.controlforge.InputProxy1.Instance.<InstanceName>
```

No enumeration or property matching is required for that case.

Enumeration is intended for clients that need to discover all currently running
proxy instances, including:

- runtime monitoring tools;
- graphical management utilities;
- `input-proxy list`.

Recommended enumeration algorithm:

1. Enumerate currently owned D-Bus names.
2. Select names beginning with
   `net.controlforge.InputProxy1.Instance.`.
3. Treat the final service-name component as the Instance Name.
4. Query public properties from matching services as required.

Clients MUST NOT enumerate services and then match a separate `DeviceName`
property to identify an instance. The Instance Name is already encoded in the
well-known service name.

## 11. Error handling

D-Bus runtime control is optional.

Failure by `input-proxy` to connect to the system bus or acquire its well-known
service name MUST NOT prevent normal evdev-to-uinput operation.

When D-Bus is unavailable:

- runtime control is unavailable;
- D-Bus runtime activity tracking is unavailable;
- the proxy continues normal input forwarding;
- startup reports a warning.

Clients MUST tolerate a running instance temporarily losing its D-Bus owner
because the process exited or restarted.

A client that already knows the Instance Name SHOULD continue to use the same
well-known service name and detect when ownership returns.

A client performing enumeration SHOULD refresh its discovered instance set when
bus-name ownership changes.

`Pause()` and `Resume()` remain idempotent whenever the service is available.

Failure to obtain required state or complete neutralization or synchronization
is fatal to the proxy session. A `Pause()` or `Resume()` call that encounters
such a failure MUST NOT return success or publish the requested transition as
successful. The process destroys the virtual device and terminates, causing its
well-known service-name ownership to disappear. Clients MUST tolerate either a
method error or service loss while the fatal cleanup completes.

## 12. Client expectations

Clients MUST NOT assume:

- a currently owned service remains continuously available;
- the process PID remains stable across restarts;
- property changes occur only because of D-Bus method calls;
- Source Availability implies forwarding is enabled;
- D-Bus availability is required for the underlying proxy to operate.

Clients MAY assume that a logical instance restarted with the same validated
Instance Name will request the same well-known D-Bus service name.

## 13. Compatibility

The public D-Bus interface is a versioned contract.

Backward-compatible changes include:

- adding new read-only properties;
- adding new signals;
- adding new optional methods.

Breaking changes include:

- removing existing members;
- changing property types;
- changing method semantics;
- changing Instance Name validation rules;
- changing service naming.

Breaking changes require a new interface version.

## 14. Example session

For a known Instance Name:

1. `input-proxy` starts with `--name HDMI-A-1-Touch`.
2. The process requests
   `net.controlforge.InputProxy1.Instance.HDMI-A-1-Touch`.
3. A client connects directly to that well-known service name.
4. The client reads runtime properties.
5. The client issues `Pause()`.
6. `Paused` and any affected activity properties report their changes through
   `PropertiesChanged`.
7. The client issues `Resume()`.
8. The proxy synchronizes the Virtual Device to current Physical Source state
   and resumes forwarding.
9. If the process exits, ownership of the well-known service name disappears.
10. If the same logical instance restarts, it requests the same well-known
    service name and the client can resume communication when ownership returns.

A client that does not already know the desired Instance Name may instead use
the Discovery procedure to enumerate currently running instances.
