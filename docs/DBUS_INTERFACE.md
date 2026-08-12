# D-Bus Interface Specification

## 1. Purpose

This document specifies the public D-Bus interface exposed by a running
`input-proxy` instance. It defines the stable runtime observation and control
contract available to external clients. Internal implementation details are
outside the scope of this document.

## 2. Design goals

- Independent proxy instances.
- Decentralized discovery.
- Minimal runtime control surface.
- Use standard D-Bus interfaces where practical.
- Do not transport raw input events.
- Runtime control augments, but does not replace, the core proxy.

## 3. Service discovery

System bus

Service name:

    net.controlforge.InputProxy1.Instance.p<PID>

Object path:

    /net/controlforge/InputProxy1/Instance

Interface:

    net.controlforge.InputProxy1.Instance

The service name is ephemeral and valid only for the lifetime of the process.
Clients should enumerate the namespace, then identify the desired instance by
reading the `DeviceName` property.

## 4. Standard interfaces

The runtime object supports:

- org.freedesktop.DBus.Properties
- org.freedesktop.DBus.Introspectable
- org.freedesktop.DBus.Peer

Project-specific functionality should not duplicate standard D-Bus interfaces.

## 5. Public properties

| Property | Type | Access | Description |
|----------|------|--------|-------------|
| DeviceName | string | Read | Configured virtual-device name. |
| Source | string | Read | Configured source path. |
| Version | string | Read | Running input-proxy version. |
| PID | uint32 | Read | Process identifier. |
| Paused | boolean | Read | Current pause state. |
| SourceAvailable | boolean | Read | Current source availability. |
| ActivityWhileRunning | boolean | Read | Retriggerable running activity indication. |
| ActivityWhilePaused | boolean | Read | Throttled paused activity indication. |

`DeviceName`, `Source`, `Version`, and `PID` remain constant for the lifetime of
the process.

`Paused`, `SourceAvailable`, `ActivityWhileRunning`, and
`ActivityWhilePaused` change through normal runtime operation and are reported
using the standard `PropertiesChanged` notification.

## 6. Methods

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

## 7. Activity semantics

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

## 8. Recommended discovery algorithm

1. Enumerate names on the system bus.
2. Filter for `net.controlforge.InputProxy1.Instance.*`.
3. Read `DeviceName`.
4. Match the desired instance.
5. Cache the object proxy while the service remains present.

## 9. Error handling

- D-Bus unavailable does not prevent proxy operation.
- Methods remain idempotent.
- Clients must tolerate services appearing and disappearing.
- Clients should rediscover instances after service loss.

## 10. Client expectations

Clients must not assume:

- Service names survive restart.
- Proxy processes run indefinitely.
- Property changes occur only because of D-Bus method calls.
- Source availability implies forwarding is enabled.

## 11. Compatibility

Adding new read-only properties is backward compatible.

Changing property types, names, method semantics, or removing existing members
constitutes a breaking API change and requires a new interface version.

## 12. Example session

1. Proxy starts.
2. Registers on the system bus.
3. Client discovers the instance.
4. Client matches `DeviceName`.
5. Client reads runtime properties.
6. Client issues `Pause()`.
7. Properties change.
8. Client issues `Resume()`.
9. Proxy exits.
10. D-Bus service disappears automatically.
