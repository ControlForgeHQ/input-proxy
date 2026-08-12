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

- begins with an alphabetic character or underscore;
- contains only letters, digits, underscores, and hyphens;
- is unique among simultaneously running proxy instances.

The Instance Name is used consistently for:

- the virtual device name;
- the D-Bus service name;
- future persistent service installation;
- runtime diagnostics and logging.

One validation routine establishes that the supplied value is suitable for every
representation derived from the Instance Name.

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
- Synchronizes the virtual device to the current physical source state before
  forwarding resumes.

## 8. Activity semantics

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

## 9. Discovery

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

## 10. Error handling

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

## 11. Client expectations

Clients MUST NOT assume:

- a currently owned service remains continuously available;
- the process PID remains stable across restarts;
- property changes occur only because of D-Bus method calls;
- Source Availability implies forwarding is enabled;
- D-Bus availability is required for the underlying proxy to operate.

Clients MAY assume that a logical instance restarted with the same validated
Instance Name will request the same well-known D-Bus service name.

## 12. Compatibility

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

## 13. Example session

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
