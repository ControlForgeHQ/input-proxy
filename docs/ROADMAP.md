# Roadmap

This document describes the planned evolution of `input-proxy`.

It is intentionally conservative. Features listed in future versions should not
be implemented early unless specifically requested.

---

# Version 0.1

## Goal

A small, robust, transparent Linux evdev-to-uinput proxy.

The virtual device should represent a stable logical proxy instance rather than
appearing and disappearing with every temporary physical-source connection.

## Required features

- one source device per process;
- configurable virtual device name;
- automatic source disconnect and reconnect;
- persistent virtual-device lifetime across temporary source disconnect and
  reconnect;
- safe neutralization of active virtual input state when the physical source is
  lost;
- source compatibility validation before resuming forwarding after reconnect;
- generic evdev capability cloning;
- transparent event forwarding;
- preservation of event ordering and synchronization boundaries;
- `SYN_DROPPED` recovery;
- clean shutdown;
- concise logging.

The physical source is considered a recoverable backing resource.

Once the virtual device has been successfully created, temporary loss of the
physical source should not normally remove the virtual device from the operating
system.

When the source disconnects, the proxy should:

- return any active virtual momentary state to a safe neutral state;
- release the physical source;
- retain the virtual device;
- wait efficiently for the configured source to return;
- validate the reconnected source against the existing virtual-device
  capabilities;
- resume forwarding without re-enumerating the virtual device when compatible.

When the source reconnects, the proxy should:

- validate the reconnected source against the existing virtual-device
  capabilities;
- retain the existing virtual device when the source is compatible;
- replace the virtual device when the source is incompatible;
- resume operation without terminating the proxy session.

Capability changes across reconnect are therefore recoverable. They may cause
intentional virtual-device re-enumeration, but should not cause `input-proxy` to
exit unless replacement virtual-device creation itself fails.
## Non-goals

Version 0.1 does not support:

- event transformation;
- selective event remapping or filtering;
- coordinate scaling;
- gesture recognition;
- multiple source devices in one process;
- configuration files;
- plugins;
- networking.

The primary objective is correctness, reliability, and stable logical device
identity across normal source-loss conditions.

---

# Version 0.2

## Goal

Improve diagnostics and operational visibility without changing the core proxy
model.

## Potential enhancements

- richer device identity and capability reporting;
- improved lifecycle and reconnect logging;
- additional integration and hardware tests;
- clearer diagnostics for unsupported or incompatible capabilities;
- visibility into source availability and persistent virtual-device state;
- optional machine-readable inspection output where useful.

Version 0.2 must not introduce configuration files or change the
one-process-per-device model.

---

# Version 0.3

## Goal

Add externally controlled pause and resume behaviour for applications such as
touchscreen display wake handling.

A paused proxy must keep the physical source open while it remains available and
keep the persistent virtual device present while suppressing delivery of source
events to the virtual device.

Temporary physical-source loss must not remove the persistent virtual device.

## Required runtime behaviour

Version 0.3 must:

- provide an explicit paused operating state;
- continue reading and processing source events while paused;
- keep the virtual uinput device present while paused;
- keep the virtual uinput device present across temporary source disconnect and
  reconnect;
- suppress paused source events rather than queueing or replaying them;
- preserve the requested paused state across source disconnect and reconnect;
- remain externally controllable while waiting for the source device;
- support starting in the paused state;
- report externally visible lifecycle and pause-state changes;
- emit a coalesced activity notification when meaningful source activity is
  detected while paused;
- avoid emitting one external notification for every raw input event;
- suppress the complete interaction that caused a wake notification;
- resume forwarding only at a clean input-state boundary;
- avoid leaving the virtual device with a stuck key, button, or touch contact
  when pausing;
- avoid leaving the virtual device with a stuck key, button, or touch contact
  when the physical source disconnects;
- remain responsive to shutdown requests in every control and lifecycle state.

Pausing changes event delivery, not source consumption. The physical source
event queue must continue to be drained while forwarding is suppressed.

Events suppressed while paused must never be replayed after resuming.

The persistent virtual-device model introduced in Version 0.1 remains
authoritative. Pause support must not introduce a second virtual-device lifetime
policy.

## Safe pause transition

A pause request received during an active interaction must not immediately stop
forwarding in a way that leaves the virtual device in a partially active state.

The proxy should enter an intermediate pausing state, continue forwarding the
current interaction, and become fully paused after the source reaches a clean
input-state boundary.

An external controller that intends to turn off a display should wait until the
proxy reports that it is fully paused before disabling the display.

## Safe resume transition

If a resume request is received while the source is already neutral, forwarding
may resume immediately.

If a resume request is received while the wake interaction is still active, the
proxy should enter an intermediate resuming state and continue suppressing
events until that interaction has ended at a clean input-state boundary.

The next new interaction may then be forwarded normally.

## Activity notification

While fully paused, the first meaningful event in a new interaction should
produce an external activity notification.

Further events belonging to the same interaction should not produce additional
notifications.

Activity notification should be re-armed after the source returns to a clean
input-state boundary. A bounded coalescing or rate-limiting mechanism may also
be used to prevent notification storms from devices that do not expose clear
press-and-release interactions.

The activity notification is a wake trigger, not a replacement event stream. It
must not expose or mirror every suppressed input event.

## Control interface

The preferred control mechanism is a local system D-Bus interface.

The control interface should support:

- requesting pause;
- requesting resume;
- explicitly setting the requested paused state;
- querying the current effective session state;
- querying whether event forwarding is currently suppressed;
- querying physical-source availability;
- querying persistent virtual-device availability;
- receiving activity notifications;
- receiving lifecycle or pause-state change notifications.

D-Bus method handlers must request session-level state changes rather than
directly modifying source or virtual-device resources.

The session remains the authority for deciding when a requested transition is
safe to complete.

The control interface should remain available while the source device is
missing. A controller must therefore be able to request a paused initial state
before the source reconnects.

The persistent virtual device should also remain available during ordinary
source-loss periods once it has been successfully created.

The D-Bus service must provide a deterministic, stable identity for each proxy
instance. It must not rely solely on a process ID or another value that changes
on every restart.

The exact well-known bus-name escaping and authorization policy should be
specified and tested as part of the D-Bus implementation issue.

## Event-loop integration

The runtime should remain single-threaded unless a demonstrated technical need
requires otherwise.

Source-device input, D-Bus requests, lifecycle transitions, and shutdown
requests should be integrated into one event loop or another explicitly
coordinated execution model.

D-Bus callbacks must not introduce unsynchronized mutation of proxy-session
state.

## Validation

Version 0.3 should include:

- hardware-independent tests for pause-state transitions;
- tests proving that suppressed events are not forwarded or replayed;
- tests for pause requests made during active input;
- tests for resume requests made during the wake interaction;
- tests for activity-notification coalescing;
- tests for disconnect and reconnect while paused;
- tests proving that the virtual device remains present across disconnect and
  reconnect while paused;
- tests for control requests while the source is unavailable;
- real touchscreen validation where suitable hardware is available.

A representative touchscreen wake sequence should be validated as follows:

1. request pause;
2. wait until the proxy reports that it is fully paused;
3. turn off the display backlight;
4. touch the physical touchscreen;
5. receive one activity notification;
6. turn on the display backlight;
7. request resume;
8. release the wake touch;
9. verify that the wake touch was not delivered to the virtual device;
10. verify that the next touch is forwarded normally.

A representative source-loss sequence while paused should also verify that:

1. the virtual device remains present;
2. the physical source is released;
3. requested pause state is preserved;
4. the source may reconnect without virtual-device re-enumeration;
5. forwarding remains suppressed until the session is safely resumed.

---

# Version 0.4

## Goal

Provide straightforward Debian-family packaging and guided system installation.

## Required features

- build a distributable Debian package;
- support installation through `dpkg`;
- support clean package removal;
- install the application binary, documentation, D-Bus support files, systemd
  template units, udev rules, and other required deployment files;
- provide an interactive installation workflow through:

```text
input-proxy --install
```

The interactive installer should:

- allow the user to select a currently available input device;
- optionally identify a device by waiting for input activity;
- request or propose a unique virtual device name;
- configure a stable logical identity for the proxy instance;
- allow the instance to start active or paused;
- create the required persistent udev source rule;
- create or configure the required systemd instance;
- install or reference the required D-Bus policy and service metadata;
- reload udev, D-Bus, and systemd configuration where required;
- enable and start the configured proxy instance;
- clearly report every persistent system change before applying it;
- provide actionable errors when installation cannot be completed.

The installer should also support a non-interactive form:

```text
input-proxy \
    --source PATH \
    --name NAME \
    --install
```

Additional non-interactive options may be introduced when required to remove
ambiguity, including an option to start the configured instance paused.

Existing proxy-mode command-line behaviour must remain compatible unless an
explicitly documented breaking change is approved.

Installation responsibilities must remain separate from normal proxy operation.
Running `input-proxy` without `--install` must never modify system
configuration.

The installation workflow may require elevated privileges for specific system
changes. Privileged operations must be explicit and narrowly scoped.

Package removal must not silently delete locally generated proxy-instance
configuration. Removal and purge behaviour should follow normal Debian
conventions:

- package removal may retain local configuration;
- package purge may remove package-managed configuration;
- generated local device mappings should be handled conservatively and
  documented clearly.

No general-purpose configuration-file format is planned. Files generated for
systemd, D-Bus, or udev are deployment artifacts, not an application
configuration interface.

---

# Explicit non-goals

The following are outside the intended scope of this project:

- compositor replacement;
- display or backlight management;
- Wayland protocol implementation;
- DRM or KMS management;
- calibration;
- coordinate transformation;
- gesture interpretation;
- general-purpose input remapping;
- macro recording;
- general automation;
- GUI configuration;
- network input transport.

The pause/activity interface may be used by external display-management or
automation software, but `input-proxy` itself must not manage displays or
backlights.

These are valuable problems, but they belong in separate projects.