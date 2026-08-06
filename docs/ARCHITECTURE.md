# Architecture

`input-proxy` is intentionally built around a single primary abstraction: the
proxy session.

Each process manages exactly one proxy session.

## High-level structure

```text
Application
    |
    v
Proxy Session
    |
    +-- Source Device
    |
    +-- Virtual Device
    |
    +-- Control Service
    |
    +-- Session Lifecycle and Event Policy
```

The external event path is:

```text
physical evdev device
    -> source device
    -> proxy-session event policy
    -> virtual uinput device
    -> libinput or another input consumer
```

External control follows a separate control path:

```text
external controller
    -> D-Bus control service
    -> proxy-session state request
```

Activity notification follows the reverse control path:

```text
physical source activity while paused
    -> proxy session
    -> D-Bus activity signal
    -> external controller
```

The data path and control path must remain distinct.

## Proxy session

A proxy session owns everything associated with proxying one physical input
device.

It is responsible for:

- the configured source path;
- the configured virtual device name;
- the logical identity of the proxy instance;
- the requested pause state;
- the current lifecycle state;
- the physical source device;
- the virtual uinput device;
- the control-service connection;
- event-forwarding policy;
- paused-event suppression;
- source disconnect and reconnect handling;
- synchronization recovery;
- orderly cleanup and shutdown.

The proxy session coordinates lower-level helpers but should not contain
platform installation logic.

Every source event must pass through session-level event policy before it is
written to the virtual device.

The source-device implementation must not write directly to the virtual device,
and the virtual-device implementation must not independently read from the
source.

This boundary is required so that pause, resume, synchronization recovery, and
future session-level policies can be applied consistently.

## Source device

The source-device component represents the physical evdev device.

It is responsible for:

- opening and closing the configured source path;
- initializing the libevdev device;
- inspecting identity and capabilities;
- reading events;
- exposing current source state required for synchronization and transition
  decisions;
- detecting device loss;
- supporting synchronization recovery after `SYN_DROPPED`.

It should not know:

- how the virtual device is created;
- whether an event will be forwarded or suppressed;
- whether the session is active, pausing, paused, or resuming;
- how external control requests are received.

The source device must continue to be read while the session is paused.

## Virtual device

The virtual-device component represents the uinput device.

It is responsible for:

- creating the virtual device;
- assigning its configured name;
- reproducing supported source identity and capabilities;
- injecting events when instructed by the proxy session;
- destroying the virtual device;
- safely cleaning up partially initialized resources.

It should not:

- discover or open physical source devices;
- decide whether an event should be forwarded;
- implement pause-state transitions;
- receive D-Bus control requests.

The virtual device normally remains present while the proxy is paused.

It may still be destroyed when the source disconnects, when creation fails, or
when the process shuts down.

## Control service

The control-service component exposes the proxy session to local external
controllers.

The preferred initial transport is the system D-Bus.

It is responsible for:

- connecting to the system bus;
- registering the proxy instance's control interface;
- translating method calls into proxy-session state requests;
- exposing current lifecycle and pause information;
- emitting activity notifications;
- emitting state or property-change notifications;
- releasing all bus resources during shutdown.

It must not:

- read source events directly;
- write virtual-device events directly;
- decide that a pause or resume transition is complete;
- mutate source or virtual-device ownership independently;
- manage display power or backlight state.

The session remains authoritative. The control service requests transitions and
reports the resulting state.

### D-Bus interface requirements

The D-Bus interface should support operations equivalent to:

```text
Pause()
Resume()
SetPaused(bool paused)
```

It should expose state equivalent to:

```text
State
Paused
SourceAvailable
VirtualDeviceAvailable
```

`Paused` means forwarding is effectively suppressed. It is not merely a record
that a pause request has been received.

The interface should emit:

```text
ActivityDetected
```

when coalesced source activity is detected while paused.

Standard D-Bus property-change notification should be used where practical
instead of inventing duplicate state-change signals.

The exact interface name, object path, well-known bus-name escaping, and bus
authorization policy must be finalized in the implementation issue and treated
as a versioned external API.

Each process must expose a stable and deterministic instance identity. A PID-only
bus identity is not sufficient because it changes after every restart.

The configured virtual-device name may be used as the default logical instance
identity, provided it is converted to a valid D-Bus name deterministically and
collisions are handled explicitly.

## Main application

The main application is responsible only for:

- parsing command-line arguments;
- selecting the requested operating mode;
- initializing process-level resources;
- installing signal handling;
- constructing and running a proxy session;
- returning an appropriate process exit status.

Normal proxy behaviour should live in the proxy-session implementation rather
than in `main.c`.

The runtime command line may eventually include an option to request an
initially paused session. That option should initialize session state; it should
not create a separate pause implementation in `main.c`.

Future installation functionality should remain a separate top-level operating
mode:

```text
main
    |
    +-- proxy mode
    |      |
    |      +-- Proxy Session
    |
    +-- install mode
           |
           +-- Installer
```

The installer must not be coupled to runtime event-forwarding implementation.

## Session lifecycle

The proxy-session lifecycle must be represented explicitly rather than through
deeply nested retry, forwarding, and control loops.

The intended states are:

```text
STARTING
WAITING_FOR_SOURCE
CREATING_PROXY
ACTIVE
PAUSING
PAUSED
RESUMING
CLEANING_UP
SHUTTING_DOWN
```

A general-purpose state-machine framework is not required. A clear state enum
and explicit transition loop are preferred.

## Core lifecycle

```text
STARTING
    |
    v
WAITING_FOR_SOURCE
    |
    | source becomes available
    v
CREATING_PROXY
    |
    | proxy created and pause not requested
    v
ACTIVE
```

If pause has already been requested when the proxy is created:

```text
CREATING_PROXY
    |
    | proxy created and pause requested
    v
PAUSED
```

Source disconnection from any connected state follows:

```text
ACTIVE / PAUSING / PAUSED / RESUMING
    |
    | source disconnected
    v
CLEANING_UP
    |
    v
WAITING_FOR_SOURCE
```

The requested pause state survives this cleanup and reconnect cycle.

Shutdown may be requested from any state:

```text
any state
    |
    | SIGINT or SIGTERM
    v
SHUTTING_DOWN
    |
    v
EXIT
```

## State responsibilities

### STARTING

- validate session configuration;
- initialize process-level resources;
- initialize the control service;
- verify required facilities;
- install signal handling.

Configuration errors and permanently unavailable required resources are fatal
at this stage.

Whether D-Bus availability is mandatory or may be disabled explicitly should
be decided as part of the Version 0.3 implementation contract. Silent loss of a
requested control interface is not acceptable.

### WAITING_FOR_SOURCE

- wait efficiently for the configured source path;
- keep the control interface responsive;
- accept pause and resume requests;
- preserve the requested pause state;
- remain responsive to shutdown;
- avoid busy-waiting;
- avoid repeatedly logging the same missing-device message.

A missing source is a normal operating condition, not an error.

### CREATING_PROXY

- open the source device;
- initialize its libevdev representation;
- inspect its identity and capabilities;
- create the corresponding virtual uinput device;
- select the initial connected state from the requested pause state.

If the source disappears during creation, release partial resources and return
to `WAITING_FOR_SOURCE`.

A device that exists but is permanently incompatible may be treated as an
error, provided the diagnostic clearly explains the incompatibility.

### ACTIVE

- read source events;
- update tracked source interaction state;
- forward events to the virtual device;
- preserve ordering and `EV_SYN` report boundaries;
- recover from `SYN_DROPPED`;
- detect source loss;
- process control and shutdown requests.

### PAUSING

`PAUSING` is entered when pause is requested while an interaction forwarded to
the virtual device is still active.

During `PAUSING`, the session must:

- continue reading source events;
- continue forwarding the remainder of the already-started interaction;
- continue updating interaction state;
- wait for a clean input-state boundary;
- transition to `PAUSED` only after the virtual device has received the releases
  or other events needed to return it to a neutral state.

This prevents a key, button, or touch contact from remaining logically stuck on
the virtual device.

A controller that plans to disable a display should wait until the effective
state becomes `PAUSED`.

If the source is already neutral when pause is requested, the session may move
directly from `ACTIVE` to `PAUSED`.

### PAUSED

During `PAUSED`, the session must:

- keep the source open;
- keep the virtual device present;
- continue consuming source events;
- update tracked source interaction state;
- suppress event delivery to the virtual device;
- never queue suppressed events for later replay;
- emit at most one activity notification for a coalesced interaction;
- remain responsive to resume, control, disconnect, and shutdown requests.

A source event received while paused is still significant for:

- maintaining current source state;
- identifying a clean input-state boundary;
- synchronization recovery;
- activity notification.

It is not significant for virtual-device delivery.

### RESUMING

`RESUMING` is entered when resume is requested while an interaction that began
during `PAUSED` remains active.

During `RESUMING`, the session must:

- continue consuming source events;
- continue updating source interaction state;
- continue suppressing the wake interaction;
- avoid emitting repeated activity notifications for that same interaction;
- wait for a clean input-state boundary;
- transition to `ACTIVE` only after the wake interaction has completely ended.

If the source is already neutral when resume is requested, the session may move
directly from `PAUSED` to `ACTIVE`.

The first interaction that begins after entering `ACTIVE` is forwarded
normally.

### CLEANING_UP

- destroy the virtual device;
- close the physical source;
- release all per-device resources;
- reset per-device event and synchronization state;
- preserve process-level control state, including the requested pause state;
- return to `WAITING_FOR_SOURCE`.

Cleanup must be safe when initialization completed only partially.

### SHUTTING_DOWN

- stop accepting new work;
- stop waiting for or reading the source;
- destroy any active virtual device;
- close the source device;
- unregister and close the control service;
- release all resources;
- exit cleanly.

## Requested versus effective pause state

The architecture distinguishes between:

```text
pause requested
```

and:

```text
forwarding effectively suppressed
```

A pause request may temporarily leave the session in `PAUSING`.

A resume request may temporarily leave it in `RESUMING`.

The control interface must expose effective state clearly enough that an
external controller can wait for a safe transition.

A simple internal model may include:

```text
pause_requested
current_state
forwarding_suppressed
```

These values must not be treated as interchangeable.

If pause is requested while the source is unavailable, `pause_requested` is
stored. The next successfully created proxy begins in `PAUSED`.

If resume is requested while the source is unavailable, the stored request is
cleared. The next successfully created proxy begins in `ACTIVE`.

## Clean input-state boundary

A clean input-state boundary is the point at which forwarding can be safely
enabled or disabled without exposing a partial interaction.

For the initial touchscreen-focused implementation, the boundary should occur
after a `SYN_REPORT` when no momentary input interaction remains active.

Tracked active state should include at least:

- `EV_KEY` codes whose current value is nonzero;
- `BTN_TOUCH` and other pressed buttons;
- active multitouch slots whose `ABS_MT_TRACKING_ID` is nonnegative.

Persistent switch state does not necessarily prevent a clean boundary.

Relative motion and other devices without an explicit held state may use a
completed `SYN_REPORT` as a boundary, subject to activity coalescing.

The interaction-state tracker belongs to the proxy session or to a focused
session-owned helper. It must not be hidden inside the D-Bus transport.

The implementation should remain generic, but touchscreen and multitouch
correctness are required validation cases.

## Activity detection and coalescing

While `PAUSED`, meaningful source activity should trigger one
`ActivityDetected` notification.

A first implementation may treat any non-`EV_SYN` source event that represents
a state or value change as meaningful activity.

The session should maintain an activity latch:

```text
armed
    |
    | first meaningful paused event
    v
notification emitted and disarmed
    |
    | clean input-state boundary
    v
armed
```

A bounded time-based coalescing interval may supplement the clean-boundary rule
for devices that produce continuous or boundary-only activity.

The control service must not emit one D-Bus signal for every raw source event.

The activity signal indicates only that local source activity occurred. It is
not a transport for the suppressed event stream.

## Wake-controller sequence

The intended external display-wake sequence is:

```text
controller requests pause
    |
    v
proxy enters PAUSING if necessary
    |
    v
proxy reports PAUSED
    |
    v
controller turns off display backlight
    |
    v
physical input occurs
    |
    v
proxy suppresses input and emits ActivityDetected
    |
    v
controller turns on display backlight
    |
    v
controller requests resume
    |
    v
proxy suppresses the remainder of the wake interaction
    |
    v
source reaches a clean boundary
    |
    v
proxy reports ACTIVE
```

`input-proxy` does not turn the display on or off. It provides the control and
activity primitives required by another component.

## Synchronization recovery while paused

`SYN_DROPPED` must be handled even while forwarding is suppressed.

After `SYN_DROPPED`, the source-device state must be resynchronized through
libevdev.

While `PAUSED` or `RESUMING`:

- recovered source state is used to rebuild internal interaction state;
- recovered events are not forwarded merely because synchronization occurred;
- forwarding remains suppressed;
- clean-boundary eligibility is reevaluated from the recovered state.

While `PAUSING`, recovery must not leave the virtual device stuck. The precise
strategy for reconciling already-forwarded virtual state should be defined in
the synchronization-recovery implementation issue.

## Event-loop and concurrency model

The preferred runtime is a single coordinated event loop.

It should process:

- source-device readiness;
- D-Bus messages;
- shutdown notifications;
- retry or reconnect timers;
- lifecycle transitions.

The initial design should not require worker threads.

If threads are introduced later, ownership and synchronization boundaries must
be documented before implementation.

D-Bus callbacks should enqueue or request session actions and return promptly.
They must not run an independent nested proxy loop.

## Transition discipline

Lifecycle transitions should be explicit and logged at an appropriate
verbosity.

Lower-level helpers should return enough information for the proxy session to
decide the next state. They should not silently perform session-level
transitions.

All state transitions should be centralized so that logging, D-Bus property
updates, cleanup, and tests observe the same state changes.

## One process per device

Each process manages exactly one proxy session and therefore proxies exactly one
source device.

Multiple devices are handled through multiple process instances, typically
managed by systemd.

This keeps runtime behaviour:

- simple;
- fault-isolated;
- independently restartable;
- easy to diagnose.

The control-plane identity must therefore distinguish concurrently running
instances reliably.

The internal session abstraction should remain self-contained, but this does
not imply future support for multiple sessions inside one process.

## Platform constraints

Some Linux evdev/uinput behaviours are constrained by the kernel interface
rather than the implementation.

Examples include:

- the virtual-device physical path (`Phys`) cannot be configured through the
  standard uinput interface;
- some source-device metadata or behaviour cannot be reproduced exactly by
  uinput;
- repeat timing may be controlled by kernel defaults rather than copied exactly.

Platform limitations discovered during hardware validation should be recorded
rather than hidden behind misleading configuration options.

## Non-goals

The runtime proxy deliberately does not understand or manage:

- Wayland;
- DRM or KMS;
- displays or monitors;
- display power or backlights;
- windows;
- touch mapping;
- calibration;
- gestures;
- coordinate transformations;
- general-purpose event remapping;
- systemd configuration;
- udev rule generation.

Pause mode is a session-level delivery gate, not a general event-filtering
language.

Display and automation responsibilities belong to software above or alongside
the runtime proxy.