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
- the persistent virtual uinput device;
- the control-service connection;
- event-forwarding policy;
- paused-event suppression;
- source disconnect and reconnect handling;
- synchronization recovery;
- virtual-device neutralization after source loss;
- source compatibility validation after reconnect;
- orderly cleanup and shutdown.

The proxy session coordinates lower-level helpers but should not contain
platform installation logic.

Every source event must pass through session-level event policy before it is
written to the virtual device.

The source-device implementation must not write directly to the virtual device,
and the virtual-device implementation must not independently read from the
source.

This boundary is required so that pause, resume, synchronization recovery,
source-loss handling, and future session-level policies can be applied
consistently.

The physical source and virtual device have intentionally different lifetimes.

The physical source is a recoverable backing resource that may connect,
disconnect, and reconnect while the session remains alive.

The virtual device is the stable logical device presented by the proxy session
to the rest of the operating system.

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

Source-device lifetime is tied to physical-source availability. Losing the
physical source must not implicitly end the proxy session or destroy the stable
logical virtual device.

## Virtual device

The virtual-device component represents the uinput device exposed to input
consumers such as libinput and Wayland compositors.

It is responsible for:

- creating the virtual device;
- assigning its configured name;
- reproducing supported source identity and capabilities;
- injecting events when instructed by the proxy session;
- exposing enough information to inspect the kernel-visible state of the
  resulting evdev device when required by session policy;
- destroying the virtual device;
- safely cleaning up partially initialized resources.

It should not:

- discover or open physical source devices;
- decide whether an event should be forwarded;
- implement pause-state transitions;
- implement source reconnect policy;
- decide when virtual state must be neutralized;
- receive D-Bus control requests.

The virtual device represents the stable logical input device exposed by the
proxy session.

Once successfully created, it should normally remain present for the lifetime
of the proxy session, including while:

- the proxy is paused;
- the physical source is temporarily unavailable;
- the physical source disconnects and reconnects.

Temporary source loss must not, by itself, cause the virtual device to disappear
and later re-enumerate.

This keeps the logical input device stable for consumers such as compositors and
avoids unnecessary device-added and device-removed cycles.

The virtual device may still be destroyed when:

- initial virtual-device creation fails;
- a reconnected source is incompatible with the capabilities represented by the
  existing virtual device and replacement is required;
- an unrecoverable virtual-device failure occurs;
- the proxy session shuts down.

Source reconnect handling must therefore distinguish between the lifetime of the
physical source and the lifetime of the virtual device.

### Virtual-device stability across source reconnect

The physical source is a recoverable backing resource for the proxy session.

The virtual device is the stable logical device presented to the rest of the
system.

The intended lifetime relationship is:

```text
proxy session
    |
    +-- virtual device
    |       created once when possible
    |       remains present across compatible source loss/reconnect
    |
    +-- physical source
            connect
            disconnect
            reconnect
            disconnect
            ...
```

When the physical source disconnects:

1. the session stops consuming events from the lost source;
2. the session queries the persistent virtual device's kernel-visible state;
3. any momentary virtual state that remains logically active is explicitly
   returned to a neutral state;
4. the physical source is closed and its per-source state discarded;
5. the virtual device remains present;
6. the session returns to waiting for the configured source;
7. when the source returns, it is reopened and validated against the existing
   virtual device;
8. forwarding resumes without recreating the virtual device when compatibility
   permits.

The proxy must not leave keys, buttons, touch contacts, multitouch contacts, or
other momentary input state logically active on the virtual device after source
loss.

Source-loss neutralization should use the kernel-visible state of the persistent
virtual evdev device as the authoritative source of current virtual state.

The session should not maintain a duplicate shadow copy of forwarded virtual
state solely for source-loss neutralization when the kernel can report that
state directly and reliably.

## Kernel-visible virtual state

The Linux input subsystem maintains current state for stateful evdev controls.

The event node created for the persistent virtual uinput device is therefore the
authoritative source for determining which momentary states are currently
visible to consumers.

When source-loss neutralization is required, the proxy should query the virtual
device's evdev state rather than infer current state from previously forwarded
events.

Relevant queryable state includes at least:

- currently pressed `EV_KEY` codes;
- `BTN_TOUCH`;
- active Type-B multitouch slots;
- each slot's current `ABS_MT_TRACKING_ID`.

A nonnegative `ABS_MT_TRACKING_ID` represents an active multitouch contact.

A value of `-1` represents an unused slot.

For source-loss neutralization, the proxy should emit only the events required
to return active momentary state to neutral.

Examples include:

```text
pressed EV_KEY code
    -> same EV_KEY code with value 0
```

and:

```text
active MT slot
    -> select ABS_MT_SLOT
    -> ABS_MT_TRACKING_ID = -1
```

If `BTN_TOUCH` is active, it should be released as part of the same
neutralization sequence.

The neutralization sequence must end with an appropriate synchronization
boundary such as `EV_SYN / SYN_REPORT`.

Ordinary retained absolute values such as `ABS_X`, `ABS_Y`,
`ABS_MT_POSITION_X`, `ABS_MT_POSITION_Y`, touch-major values, or other
non-active positional state do not need to be reset merely because the source
was lost.

A blanket "set all values to zero" strategy is not acceptable because zero may
be a meaningful non-neutral value for persistent or absolute state.

Kernel-visible state should be treated as authoritative for source-loss
neutralization unless a specific Linux input behavior is discovered that cannot
be queried reliably through the virtual event node.

Such limitations must be documented explicitly before introducing parallel
shadow state.

## Source compatibility after reconnect

A persistent virtual device can remain valid only if the reconnected physical
source is compatible with the capabilities represented by that virtual device.

A reconnect must never silently expose events that the existing virtual device
cannot represent correctly.

When the reconnected source is compatible with the existing virtual device:

- retain the existing virtual device;
- reinitialize per-source state;
- resume forwarding without virtual-device re-enumeration.

When the reconnected source is incompatible with the existing virtual device:

1. safely neutralize any active virtual input state if required;
2. destroy the existing virtual device;
3. create a new virtual device using the capabilities of the newly connected
   source;
4. initialize per-source event and synchronization state;
5. resume normal operation.

Capability incompatibility during reconnect is therefore a recoverable lifecycle
condition, not a fatal session error.

The proxy session should terminate only if creation of the replacement virtual
device fails with an unrecoverable error.

For an initial implementation, compatibility may require the relevant evdev
identity and capability set to match the source from which the virtual device
was created.

Compatibility policy should consider at least the properties that affect
correct event representation, including:

- supported event types;
- supported event codes;
- absolute-axis definitions;
- relevant input properties;
- other capability metadata used when constructing the virtual device.

The exact compatibility comparison should be defined by the implementation task
that introduces persistent virtual-device lifetime.

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
SOURCE_LOST
CLEANING_UP
SHUTTING_DOWN
```

A general-purpose state-machine framework is not required. A clear state enum
and explicit transition loop are preferred.

`SOURCE_LOST` and `CLEANING_UP` have different purposes.

`SOURCE_LOST` handles recoverable physical-source loss while preserving the
logical proxy instance and its virtual device.

`CLEANING_UP` handles final or unrecoverable session teardown.

## Core lifecycle

Initial startup follows:

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
    | source opened
    | virtual device created or validated
    | pause not requested
    v
ACTIVE
```

If pause has already been requested when the proxy becomes connected:

```text
CREATING_PROXY
    |
    | source opened
    | virtual device created or validated
    | pause requested
    v
PAUSED
```

Source disconnection from any connected state follows:

```text
ACTIVE / PAUSING / PAUSED / RESUMING
    |
    | source disconnected
    v
SOURCE_LOST
    |
    | kernel-visible virtual state queried
    | active virtual state neutralized
    | physical source released
    | virtual device retained
    v
WAITING_FOR_SOURCE
```

When the source returns:

```text
WAITING_FOR_SOURCE
    |
    | source becomes available
    v
CREATING_PROXY
    |
    | source reopened
    | compatibility validated
    | virtual device retained or replaced as required
    v
ACTIVE / PAUSED
```

The requested pause state survives the source-loss and reconnect cycle.

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

An unrecoverable session failure may transition through:

```text
any active lifecycle state
    |
    | fatal failure
    v
CLEANING_UP
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
- keep the persistent virtual device present if one has already been created;
- keep the control interface responsive;
- accept pause and resume requests;
- preserve the requested pause state;
- remain responsive to shutdown;
- avoid busy-waiting;
- avoid repeatedly logging the same missing-device message.

A missing source is a normal operating condition, not an error.

If the virtual device already exists from an earlier connection, source absence
must not cause it to be destroyed or recreated.

Before the first successful source connection, the virtual device may not yet
exist because its capabilities are derived from the physical source.

### CREATING_PROXY

- open the source device;
- initialize its libevdev representation;
- inspect its identity and capabilities;
- create the virtual uinput device if one does not already exist;
- otherwise validate that the reconnected source is compatible with the
  existing virtual device;
- initialize per-source event and synchronization state;
- select the connected state from the requested pause state.

If the source disappears during creation, release only partial source resources
and return to `WAITING_FOR_SOURCE`.

An already-existing persistent virtual device must not be destroyed merely
because the reconnect attempt failed transiently.

If an existing virtual device cannot safely represent the capabilities of the
reconnected source, the session must:

- neutralize any active momentary state if required;
- destroy the existing virtual device;
- create a replacement virtual device from the new source capabilities;
- continue the session using the replacement device.

This replacement is a recoverable lifecycle transition and must not terminate
the proxy session merely because the physical source capabilities changed.

### ACTIVE

- read source events;
- forward events through session-level event policy;
- preserve ordering and `EV_SYN` report boundaries;
- recover from `SYN_DROPPED`;
- detect source loss;
- process control and shutdown requests.

The session does not need to maintain a duplicate copy of all forwarded
virtual-device state merely to support future source-loss neutralization.

### PAUSING

`PAUSING` is entered when pause is requested while an interaction forwarded to
the virtual device is still active.

During `PAUSING`, the session must:

- continue reading source events;
- continue forwarding the remainder of the already-started interaction;
- determine when the source has reached a clean input-state boundary;
- transition to `PAUSED` only after the virtual device has received the releases
  or other events needed to return it to a neutral state.

This prevents a key, button, or touch contact from remaining logically stuck on
the virtual device.

A controller that plans to disable a display should wait until the effective
state becomes `PAUSED`.

If the source is already neutral when pause is requested, the session may move
directly from `ACTIVE` to `PAUSED`.

The exact mechanism used to determine source interaction state during pause
transitions should prefer authoritative kernel/libevdev state where practical
rather than introducing duplicate state tracking by default.

### PAUSED

During `PAUSED`, the session must:

- keep the source open while it remains available;
- keep the virtual device present;
- continue consuming source events;
- suppress event delivery to the virtual device;
- never queue suppressed events for later replay;
- maintain enough authoritative source-state knowledge to determine clean
  interaction boundaries;
- emit at most one activity notification for a coalesced interaction;
- remain responsive to resume, control, disconnect, and shutdown requests.

A source event received while paused is still significant for:

- identifying source activity;
- synchronization recovery;
- determining a clean input-state boundary;
- activity notification.

It is not significant for virtual-device delivery.

If the source disappears while paused, the requested paused state is preserved,
the virtual device remains present, and the session transitions through
`SOURCE_LOST` to `WAITING_FOR_SOURCE`.

### RESUMING

`RESUMING` is entered when resume is requested while an interaction that began
during `PAUSED` remains active.

During `RESUMING`, the session must:

- continue consuming source events;
- continue suppressing the wake interaction;
- avoid emitting repeated activity notifications for that same interaction;
- determine when the source reaches a clean input-state boundary;
- transition to `ACTIVE` only after the wake interaction has completely ended.

If the source is already neutral when resume is requested, the session may move
directly from `PAUSED` to `ACTIVE`.

The first interaction that begins after entering `ACTIVE` is forwarded
normally.

The implementation should prefer querying authoritative source kernel/libevdev
state where practical rather than maintaining a full duplicate event-state
model solely to determine whether the wake interaction remains active.

### SOURCE_LOST

`SOURCE_LOST` is entered when the active physical source disconnects.

During `SOURCE_LOST`, the session must:

- preserve the virtual device whenever safely possible;
- query the persistent virtual device's kernel-visible evdev state;
- explicitly neutralize active momentary virtual state;
- close and release the physical source;
- reset per-source event and synchronization state;
- preserve session-level state, including requested pause state;
- transition to `WAITING_FOR_SOURCE`.

Source loss is not normal session shutdown.

The virtual device should not be destroyed merely because the physical source
temporarily disappeared.

Kernel-visible virtual state is authoritative for source-loss neutralization.

The neutralization process must inspect the current virtual state after source
loss and emit only the releases or contact-termination events required to
restore a safe neutral state.

For `EV_KEY`, all currently active momentary keys or buttons should be released.

For Type-B multitouch devices, every slot with a nonnegative
`ABS_MT_TRACKING_ID` should be terminated by selecting the slot and emitting
`ABS_MT_TRACKING_ID = -1`.

`BTN_TOUCH` and other active momentary buttons must also be released.

The sequence must end with `SYN_REPORT`.

Persistent or positional absolute state does not need to be cleared merely
because the source disappeared.

The session must not rely solely on the physical source driver emitting release
events during device teardown. Such events may be forwarded when present, but
source-loss handling must remain correct even if the source disappears without
providing a complete neutralizing event sequence.

### CLEANING_UP

`CLEANING_UP` is used for final or unrecoverable session teardown rather than
ordinary source reconnect handling.

It must:

- stop normal event processing;
- close the physical source if present;
- destroy the virtual device if present;
- release all remaining per-device resources;
- reset event and synchronization state;
- prepare the session for final shutdown or fatal exit.

Cleanup must be safe when initialization completed only partially.

Normal source disconnect and reconnect must not pass through `CLEANING_UP`
merely to destroy and recreate the virtual device.

### SHUTTING_DOWN

- stop accepting new work;
- stop waiting for or reading the source;
- close the physical source if present;
- destroy the persistent virtual device;
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
stored. The next successfully connected source begins in `PAUSED`.

If resume is requested while the source is unavailable, the stored request is
cleared. The next successfully connected source begins in `ACTIVE`.

The persistent virtual device remains present across these source-unavailable
states once it has been successfully created.

## Clean input-state boundary

A clean input-state boundary is the point at which forwarding can be safely
enabled or disabled without exposing a partial interaction.

For the initial touchscreen-focused implementation, the boundary should occur
after a `SYN_REPORT` when no momentary input interaction remains active.

Relevant active state includes at least:

- `EV_KEY` codes whose current value is nonzero;
- `BTN_TOUCH` and other pressed buttons;
- active multitouch slots whose `ABS_MT_TRACKING_ID` is nonnegative.

Persistent switch state does not necessarily prevent a clean boundary.

Relative motion and other devices without an explicit held state may use a
completed `SYN_REPORT` as a boundary, subject to activity coalescing.

Where the source remains available, current physical-source state should be
obtained from authoritative kernel/libevdev state where practical.

The architecture does not require a continuously maintained shadow copy of
incoming state if the kernel can provide the required current state reliably.

A focused session-owned state helper may still be introduced later if a
specific transition or activity-coalescing requirement cannot be implemented
correctly from kernel-visible state alone.

Any such helper must have a documented reason for existing and must not become a
second competing source of truth.

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

The activity latch is control-flow state, not a duplicate representation of
the physical device's complete input state.

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

- recovered source state is used to restore authoritative knowledge of current
  source state;
- recovered events are not forwarded merely because synchronization occurred;
- forwarding remains suppressed;
- clean-boundary eligibility is reevaluated from the recovered state.

While `PAUSING`, recovery must not leave the virtual device stuck. Recovered
events must pass through the same session-level event policy used by ordinary
source events.

Synchronization recovery and source-loss neutralization solve different
problems:

- synchronization recovery reconstructs the current physical-source state after
  source events were lost;
- source-loss neutralization queries and corrects the current persistent
  virtual-device state after the physical source is no longer available.

Neither mechanism should require a duplicate shadow event-state model when the
kernel and libevdev can provide the necessary authoritative state.

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

Resource lifetimes must follow the lifecycle model explicitly:

- source resources follow physical-source availability;
- the virtual device follows proxy-session lifetime whenever compatible and
  healthy;
- control resources follow proxy-session lifetime.

Kernel-visible input state should remain authoritative wherever the operating
system exposes the state needed for a lifecycle decision.

## One process per device

Each process manages exactly one proxy session and therefore proxies exactly one
logical source device.

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

A temporary disconnect does not change the logical identity of the proxy
instance.

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

The persistent virtual-device model intentionally avoids unnecessary kernel
device removal and re-creation when the backing physical source disappears
temporarily.

Hardware validation has confirmed that current `EV_KEY` state and Type-B
multitouch slot state can be queried from the virtual evdev node after being
written through uinput. This supports using the kernel-visible virtual state as
the source of truth for source-loss neutralization.

The implementation must still handle platforms or device classes conservatively
if future testing identifies state that cannot be queried or neutralized safely
through the standard evdev/uinput interfaces.

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