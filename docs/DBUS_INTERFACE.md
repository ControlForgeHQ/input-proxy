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

The runtime-control interface provides synchronous, deterministic, idempotent
state requests.

| Method | Input signature | Output signature | Successful completion |
|--------|-----------------|------------------|-----------------------|
| `Pause` | `()` | `()` | The requested paused state has been committed. |
| `Resume` | `()` | `()` | The requested unpaused state has been committed. |

The corresponding project-specific introspection members are:

```xml
<method name="Pause"/>
<method name="Resume"/>
```

Neither method accepts options, returns state, or supports no-reply semantics.
Clients obtain resulting runtime state through the public properties.

For a successful state change, the service performs operations in this order:

1. complete required neutralization or synchronization;
2. commit the new `Paused` value;
3. emit the corresponding `PropertiesChanged` notification;
4. send the successful method reply.

The successful reply confirms that the requested state has been committed.
Clients reconstructing state after a connection or service restart SHOULD read
the current properties rather than relying on earlier notifications.

Process lifetime remains outside the requested control surface. Fatal method
failure may nevertheless terminate the process as defined below.

### Pause()

`Pause()` requests paused operation.

When the source is available, successful completion means:

- required neutralization has completed;
- `Paused` is `true`;
- source events continue to be consumed and are suppressed.

When the source is unavailable, `Pause()` records `Paused=true` without treating
Source Availability as a method failure. A returning source remains suppressed.

Calling `Pause()` while already paused succeeds without repeating
neutralization.

### Resume()

`Resume()` requests unpaused operation.

When the source is available, successful completion means:

- required synchronization has completed, including its final `SYN_REPORT`;
- `Paused` is `false`;
- normal forwarding may begin.

When the source is unavailable, `Resume()` records `Paused=false` without
treating Source Availability as a method failure. `SourceAvailable` remains
false, no forwarding occurs, and required synchronization precedes forwarding
when a source returns.

Calling `Resume()` while already unpaused succeeds without repeating
synchronization. Activity that occurred while forwarding was suppressed is not
replayed.

### Method errors

The methods define the following service-level errors:

| Error | Meaning | Session outcome |
|-------|---------|-----------------|
| `net.controlforge.InputProxy1.Error.StateCorrectionFailed` | Required neutralization or synchronization could not be completed safely. | The virtual device is destroyed and the session terminates. |

`StateCorrectionFailed` covers unavailable or incomplete mandatory state, a
corrective event-write failure, failure to emit the final synchronization
boundary, and source loss during a partially applied synchronization. The
human-readable error message and runtime diagnostics SHOULD identify the failed
operation without creating additional public error names.

An operation that fails with `StateCorrectionFailed` does not commit a new
`Paused` value or emit a successful property transition.

When state correction fails during a method call, the service SHOULD attempt to
return `net.controlforge.InputProxy1.Error.StateCorrectionFailed` before fatal
cleanup disconnects it from the bus. Delivery cannot be guaranteed while the
service is terminating. Clients MUST treat receipt of `StateCorrectionFailed`
and loss of the service owner before a reply as failed operations with the same
fatal session outcome.

Standard errors generated by D-Bus transport or dispatch remain defined by
D-Bus and are not additional project method errors.

## 8. Authorization

System-bus policy is authoritative for all access to the exported interface,
including public properties, standard D-Bus interfaces, `Pause()`, and
`Resume()`. The policy determines which clients may send messages to each
instance and which runtime processes may own project service names.

`input-proxy` MUST NOT apply a second authorization rule based on caller Unix
credentials, user or group identifiers, polkit, or interactive confirmation. A
method call delivered to the service by the system bus is authorized and is
processed according to the method and session-state semantics defined by this
interface.

Rejected clients receive the access error selected by the system bus without
the request reaching `input-proxy`. Such transport-policy errors are standard
D-Bus errors and are not service-level method errors defined by this interface.

Version 0.3 does not install or modify system-bus policy. An administrator or
development environment must provide policy that permits the intended runtime
and client access. Supported persistent policy installation is Version 0.4
deployment work. Missing or restrictive policy may make D-Bus integration
unavailable, but MUST NOT prevent normal evdev-to-uinput forwarding.

## 9. Activity semantics

Runtime activity is derived only from eligible physical-source input. Virtual
events generated for neutralization or synchronization are not activity.
`SYN_DROPPED` detection and events reconstructed during synchronization recovery
are not activity.

Activity eligibility distinguishes interaction from motion.

Interaction activity includes:

- `EV_KEY` events, including keyboard keys, buttons, `BTN_TOUCH`, and tool
  state;
- `EV_SW` state changes;
- Type-B multitouch contact lifecycle transitions represented by
  `ABS_MT_TRACKING_ID`.

Both assertion and release/end transitions count as interaction activity. A key
release, button release, touch release, or multitouch contact termination is
therefore activity.

Motion activity includes:

- `EV_REL` events;
- `EV_ABS` position and continuously varying axis events, including ordinary
  absolute coordinates and multitouch position, pressure, size, orientation,
  and similar contact-description values.

`ABS_MT_SLOT` is multitouch protocol bookkeeping and is not activity by itself.

`ABS_MT_TRACKING_ID` is not classified as motion because it represents contact
lifecycle. A tracking identifier beginning or ending a contact remains eligible
interaction activity even when motion activity is disabled.

`EV_SYN` events are framing/recovery protocol events and do not count as
activity by themselves.

Other ordinary physical-source events that are neither protocol bookkeeping nor
motion remain eligible activity. The activity mechanism does not attempt to
infer higher-level user intent from event values.

Motion eligibility is configured independently for running and paused
operation:

    --running-motion-activity on|off
    --paused-motion-activity on|off

Both options default to:

    on

When the corresponding option is `on`, eligible interaction and motion events
both count as activity.

When the corresponding option is `off`, motion events do not assert or
retrigger the activity indication for that mode. Interaction events remain
eligible.

The activity properties are mode-scoped and MUST remain mutually exclusive:

| `Paused` | Property eligible to become `true` | Property forced to `false` |
|----------|------------------------------------|----------------------------|
| `false` | `ActivityWhileRunning` | `ActivityWhilePaused` |
| `true` | `ActivityWhilePaused` | `ActivityWhileRunning` |

Source Availability does not select the activity mode. No new indication can be
triggered while the source is unavailable because no physical-source events are
received.

### Running activity

Controlled by:

    --activity-timeout-ms
    --running-motion-activity

Defaults:

    --activity-timeout-ms 5000
    --running-motion-activity on

Meaning:

- Eligible activity sets `ActivityWhileRunning=true`.
- Each additional eligible activity restarts the timer.
- Ineligible motion while `--running-motion-activity=off` does not assert or
  restart the timer.
- The property returns to `false` only after the timeout expires without further
  eligible activity.

### Paused activity

Controlled by:

    --detection-throttle-ms
    --paused-motion-activity

Defaults:

    --detection-throttle-ms 250
    --paused-motion-activity on

Meaning:

- Eligible activity sets `ActivityWhilePaused=true`.
- Additional eligible activity during the throttle interval does not extend the
  interval.
- Ineligible motion while `--paused-motion-activity=off` does not assert a new
  indication.
- The property returns to `false` when the throttle expires.
- A later eligible activity may assert it again.

### Pause-state transitions

When a successful pause transition commits `Paused=true`, the service MUST:

1. cancel the running-activity timer;
2. set `ActivityWhileRunning=false`;
3. commit `Paused=true`;
4. report all changed properties in one `PropertiesChanged` notification;
5. send the successful method reply.

Subsequent eligible physical-source events may assert only
`ActivityWhilePaused` and use the paused-mode motion-activity setting.

When a successful resume transition commits `Paused=false`, the service MUST:

1. cancel the paused-activity throttle interval;
2. set `ActivityWhilePaused=false`;
3. complete required synchronization when a source is available;
4. commit `Paused=false`;
5. report all changed properties in one `PropertiesChanged` notification;
6. send the successful method reply.

Subsequent eligible physical-source events may assert only
`ActivityWhileRunning` and use the running-mode motion-activity setting.
Resuming while the source is unavailable still clears
`ActivityWhilePaused`; it does not assert running activity.

A timer belongs to the pause mode in which it was created. Leaving that mode
cancels it. A canceled or expired timer MUST NOT update a property after a later
pause-state transition.

Idempotent method calls do not disturb the active mode's timer or indication.
Calling `Pause()` while already paused does not clear or restart
`ActivityWhilePaused`. Calling `Resume()` while already unpaused does not clear
or restart `ActivityWhileRunning`. The inactive property remains false.

If required neutralization or synchronization fails, the pause-state transition
is not committed and no successful activity-property transition is published.
Fatal cleanup proceeds as defined in the Methods section.

### Source loss

Source loss does not change the selected activity mode. An indication already
active for that mode completes its existing interval normally. No new indication
is triggered while the source is absent. Source-loss neutralization is generated
on the virtual side and does not count as activity.

### D-Bus initialization

When D-Bus activity tracking is initialized or reinitialized, both activity
properties start as `false`. Earlier intervals are not reconstructed, and
activity observed while D-Bus integration was unavailable is not replayed. The
first eligible physical-source event after initialization begins the indication
for the current pause mode.

When a pause-state transition clears an active indication, the corresponding
`PropertiesChanged` notification includes both `Paused` and the cleared activity
property. Properties whose values did not change are not included.

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

D-Bus integration is available only after the complete runtime object has been
exported and the process owns its well-known service name. The service-name
request MUST NOT queue behind an existing owner. Partial initialization is
cleaned up and is not exposed through the connection's unique bus name.

Initialization failures are classified as follows:

| Category | Meaning | Runtime outcome |
|----------|---------|-----------------|
| System bus unavailable | The system bus endpoint is absent or unreachable. | D-Bus integration remains unavailable; forwarding continues; periodic reinitialization is scheduled. |
| Bus connection rejected | Authentication or connection policy prevents access to the system bus. | D-Bus integration remains unavailable; forwarding continues; periodic reinitialization is scheduled. |
| Service-name ownership denied | System-bus policy does not permit the process to own its derived name. | D-Bus integration remains unavailable; forwarding continues; periodic reinitialization is scheduled. |
| Service name already owned | Another bus connection owns the exact derived name. | D-Bus integration remains unavailable; forwarding continues; periodic reinitialization is scheduled. |
| Invalid derived identifier | A validated Instance Name unexpectedly produces an invalid D-Bus identifier. | D-Bus integration is disabled; forwarding continues; automatic reinitialization is not scheduled. |
| Object export or initialization failure | Object registration, interface setup, or another internal D-Bus initialization step fails. | Partial D-Bus state is cleaned up; forwarding continues; periodic reinitialization is scheduled when the failure is recoverable. |

Startup MUST report a warning that distinguishes these categories and includes
the affected service name or initialization stage where relevant. Platform
error codes are diagnostic details and are not part of this public contract.

A service-name collision is a D-Bus namespace or deployment conflict. It is not
the normal result for another conforming `input-proxy` process with the same
Instance Name because authoritative Instance Name ownership is acquired before
D-Bus initialization. The collision does not invalidate that ownership or stop
input forwarding.

An invalid derived identifier after successful Instance Name validation is an
internal project invariant violation. The diagnostic MUST identify derivation
or validation as defective rather than report invalid operator input. Because
waiting cannot make the derived identifier valid, this failure does not enter
periodic D-Bus reinitialization.

When D-Bus initialization is unavailable because of a recoverable failure:

- runtime control is unavailable;
- D-Bus runtime activity tracking is unavailable;
- the proxy continues operating according to its current session-owned runtime
  policy;
- complete D-Bus initialization is retried periodically.

The same recovery behavior applies whether D-Bus initialization failed during
process startup or an established D-Bus connection was later lost. Recovery
does not depend on D-Bus having initialized successfully earlier in the process
lifetime.

The recovery interval is 5000 ms.

Recovery MUST use the normal runtime wait/deadline mechanism and MUST NOT block
input processing while waiting to retry. Repeated failures of the same
unchanged condition MUST NOT produce an identical standard warning on every
retry interval.

Each recovery attempt repeats complete D-Bus initialization. Partial state from
a failed attempt is cleaned up before a later attempt. Runtime control becomes
available only after the complete runtime object has been exported and the
process has acquired its well-known service name without queueing.

If an established connection is lost, runtime control and D-Bus activity
tracking become unavailable immediately, and activity timers and state are
discarded. Session-owned runtime policy, including pause state and source
availability, is unaffected.

When D-Bus initialization or reinitialization later succeeds, the public
properties expose the current session-owned state. Both activity properties
start at `false`; earlier activity intervals are not reconstructed, and activity
observed while D-Bus integration was unavailable is not replayed.

Clients MUST tolerate an instance losing its D-Bus owner because its connection
was lost or its process exited or restarted.

A client that already knows the Instance Name SHOULD continue to use the same
well-known service name and detect when ownership returns.

A client performing enumeration SHOULD refresh its discovered instance set when
bus-name ownership changes.

Clients may infer that an owned well-known service name exposes the complete
public interface. They MUST NOT infer why an expected name is absent. Absence
may mean that the proxy is not running or that D-Bus connection, policy,
ownership, or initialization failed.

Method completion, idempotence, and fatal state-correction errors are defined in
the Methods section.

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
