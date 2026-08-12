# Architecture

`input-proxy` is a Linux evdev-to-uinput proxy built around one primary runtime
abstraction: the proxy session.

Each runtime process manages exactly one logical proxy instance.

This document defines system structure, ownership boundaries, lifecycle
semantics, CLI operating modes, and architectural invariants.

Product sequencing belongs in `docs/ROADMAP.md`.

Engineering and contribution standards belong in `docs/ENGINEERING.md`.

## High-level structure

The runtime data path is:

```text
physical evdev device
    -> source device
    -> proxy session
    -> virtual uinput device
    -> libinput or another input consumer
```

The proxy session is the policy boundary between source input and virtual output.

The application also provides read-only diagnostic operating modes:

```text
input subsystem
    -> device discovery / inspection
    -> human-readable diagnostics
```

Future control functionality follows a separate control path:

```text
external controller
    -> control service
    -> proxy-session state request
```

Data flow, diagnostics, runtime policy, and external control must remain
architecturally distinct.

## Command-line architecture

`input-proxy` uses a subcommand-oriented CLI.

The primary command structure is:

```text
input-proxy run --source PATH --name NAME [--verbose]

input-proxy list

input-proxy inspect PATH

input-proxy --help
input-proxy --version
```

Each subcommand may provide command-specific `--help`.

### `run`

`run` starts the runtime proxy.

It owns no device-policy implementation itself. The application parses
arguments, constructs a proxy session, installs process-level signal handling,
runs the session, and returns its result.

Runtime lifecycle belongs in the proxy-session layer.

Before runtime startup output or device lifecycle begins, the proxy session
acquires exclusive ownership of the configured virtual-device name. Ownership
is represented by a bound Linux abstract Unix-domain socket whose address
contains a fixed application namespace followed by the exact configured name.
The atomic socket bind makes concurrent acquisition race-safe. The session
holds the socket for its lifetime, and closing the descriptor on cleanup—or
kernel cleanup after abnormal process termination—immediately releases the
name. Because the address is not a filesystem path, spaces and punctuation are
preserved without creating path-traversal or stale-file behavior.

This ownership namespace is specific to `input-proxy` runtime processes. Linux
input devices with the same displayed evdev name do not participate in it.

### `list`

`list` performs read-only device discovery.

It should present a concise set of physical input devices that are plausible
proxy sources.

Virtual uinput devices should normally be excluded.

The command is intended for human source selection rather than exhaustive
hardware enumeration.

### `inspect`

`inspect PATH` performs read-only inspection of one source candidate.

Inspection may report:

- device identity;
- input classification;
- bus/vendor/product information;
- relevant capabilities;
- permission state;
- udev properties;
- libinput configuration;
- proxy readiness;
- suggested corrective configuration.

Inspection must not:

- create a virtual device;
- start a proxy session;
- modify udev rules;
- change permissions;
- modify system configuration.

Suggestions are diagnostic output only.

### Future operating modes

Future installation functionality may add another top-level command, for
example:

```text
input-proxy install
```

Installation must remain separate from normal runtime and diagnostic modes.

Normal `run`, `list`, and `inspect` operation must not make persistent system
changes.

## Main application

The main application is a dispatcher.

It is responsible for:

- parsing global arguments;
- selecting the requested subcommand;
- parsing subcommand-specific arguments;
- printing help and version information;
- initializing process-level resources;
- installing signal handling for runtime operation;
- invoking the appropriate subsystem;
- returning an appropriate process exit status.

Conceptually:

```text
main
    |
    +-- run
    |     |
    |     +-- Proxy Session
    |
    +-- list
    |     |
    |     +-- Device Discovery
    |
    +-- inspect
    |     |
    |     +-- Device Inspection
    |
    +-- future install
          |
          +-- Installer
```

`main.c` should not become the implementation location for proxy lifecycle,
device inspection, enumeration, or installation policy.

## Proxy session

A proxy session owns the complete runtime lifecycle of one logical proxied input
device.

It owns:

- configured source path;
- configured virtual-device name;
- current physical source device;
- persistent virtual device;
- runtime lifecycle state;
- event-forwarding policy;
- reconnect policy;
- synchronization recovery;
- source-loss neutralization;
- shutdown handling.

Future runtime-control state also belongs to the session.

Every source event must pass through session-level policy before being written to
the virtual device.

The source-device layer must not write directly to the virtual device.

The virtual-device layer must not independently read the physical source.

This boundary ensures that reconnect handling, synchronization recovery,
neutralization, pause/resume policy, and future runtime control are applied
consistently.

## Source device

The source-device component represents one physical evdev source.

It is responsible for:

- opening the configured source path;
- initializing libevdev state;
- exposing source identity and capabilities;
- reading normal events;
- reading synchronization-recovery events;
- reporting source loss;
- reporting source-access failures accurately;
- releasing source resources.

It should report what happened, not decide runtime policy.

For example, the source-device layer may distinguish:

```text
source unavailable
permission denied
source disconnected
generic open failure
read failure
```

The proxy session decides whether a particular result is recoverable in the
current lifecycle context.

The source-device component must not know:

- whether a virtual device will be retained or replaced;
- whether events are currently forwarded or suppressed;
- how reconnect retries are scheduled;
- how external control requests are received.

Source-device lifetime follows physical-source availability.

Losing the physical source must not automatically end the proxy session.

## Virtual device

The virtual-device component represents the uinput-backed logical device exposed
to input consumers.

It is responsible for:

- creating the uinput device;
- assigning its configured name;
- reproducing relevant source identity and capabilities;
- writing events when instructed by the proxy session;
- exposing kernel-visible virtual state required by session policy;
- comparing relevant source capabilities where needed;
- destroying the virtual device;
- safely releasing partially initialized resources.

It must not:

- discover physical sources;
- open source devices;
- decide whether events should be forwarded;
- implement reconnect policy;
- implement pause/resume policy;
- decide when source-loss neutralization is required.

## Physical and virtual device lifetimes

The physical source and virtual device intentionally have different lifetimes.

The physical source is a recoverable backing resource.

The virtual device is the stable logical device presented to the rest of the
system.

Their intended relationship is:

```text
proxy session
    |
    +-- persistent virtual device
    |       remains present across compatible source loss/reconnect
    |
    +-- physical source
            connect
            disconnect
            reconnect
            disconnect
            ...
```

Once successfully created, the virtual device should normally remain present
until:

- the session shuts down;
- an unrecoverable virtual-device failure occurs;
- the reconnected source is incompatible and replacement is required.

Temporary source loss alone must not remove the virtual device.

This avoids unnecessary device-added/device-removed cycles in consumers such as
libinput and Wayland compositors.

## Source-loss handling

When the physical source disconnects:

1. stop consuming events from the lost source;
2. query the persistent virtual device's kernel-visible state;
3. return any active momentary virtual state to neutral;
4. close and discard the physical source;
5. retain the virtual device;
6. return to waiting for the configured source.

The proxy must not leave momentary state logically active after source loss.

Examples include:

- pressed keys;
- pressed buttons;
- `BTN_TOUCH`;
- active multitouch contacts.

## Kernel-visible virtual state

The Linux input subsystem maintains current state for stateful evdev controls.

The event node corresponding to the persistent virtual uinput device is the
authoritative source for virtual state used during source-loss neutralization.

The session should query kernel-visible state rather than maintaining a parallel
shadow copy solely for neutralization.

Relevant state includes:

- active `EV_KEY` values;
- `BTN_TOUCH`;
- Type-B multitouch slots;
- `ABS_MT_TRACKING_ID`.

For Type-B multitouch:

```text
tracking_id >= 0
    -> active contact

tracking_id == -1
    -> unused slot
```

Neutralization should emit only events required to make active momentary state
inactive.

For example:

```text
pressed EV_KEY
    -> EV_KEY value 0
```

and:

```text
active multitouch slot
    -> ABS_MT_SLOT
    -> ABS_MT_TRACKING_ID = -1
```

If `BTN_TOUCH` is active, it must also be released.

The sequence must end with an appropriate synchronization boundary.

Retained absolute coordinates such as `ABS_X`, `ABS_Y`,
`ABS_MT_POSITION_X`, and `ABS_MT_POSITION_Y` are not inherently active state and
must not be blindly reset to zero.

A blanket reset-all-values strategy is not acceptable.

Parallel shadow state should be introduced only if a concrete Linux input state
is demonstrated to be unavailable or unreliable through the kernel-visible
virtual device.

## Reconnect

After source loss, the session waits for the configured source to return.

When the source reappears:

1. reopen it;
2. initialize source state;
3. compare it with the existing virtual device;
4. retain or replace the virtual device as required;
5. resume forwarding.

A reconnect is a lifecycle transition, not process reinitialization.

## Reconnect settling

Linux hotplug may expose a device node before udev has finished applying its
final permissions or other device properties.

The session therefore distinguishes between:

- initial source acquisition;
- reacquisition of a source that previously opened successfully.

An initial permission failure is treated as a real configuration/access error.

A permission failure during reacquisition of a previously working source may be
treated as transient for a bounded settling period.

During that period:

- retain the persistent virtual device;
- remain in source-wait/reconnect lifecycle;
- retry using a bounded cadence;
- remain responsive to shutdown.

Persistent permission denial after the settling period is fatal.

This behavior exists specifically to tolerate demonstrated hotplug/udev
ordering and must not turn persistent permission problems into infinite retries.

## Source compatibility after reconnect

A persistent virtual device may be retained only if it can correctly represent
the reconnected source.

Compatibility must consider properties that affect event representation,
including where relevant:

- event types;
- event codes;
- absolute-axis definitions;
- input properties;
- identity/capability metadata used to construct the virtual device.

If the source is compatible:

```text
retain virtual device
    -> initialize new source
    -> resume forwarding
```

If the source is incompatible:

```text
new source
    -> destroy old virtual device
    -> create replacement from new source
    -> resume forwarding
```

Capability incompatibility is recoverable.

The session should terminate only if the required replacement virtual device
cannot be created or another unrecoverable failure occurs.

## Event forwarding

Normal runtime event flow is:

```text
source read
    -> proxy-session policy
    -> virtual-device write
```

Event order must be preserved.

`EV_SYN` boundaries must be preserved.

The proxy is not an event-remapping engine.

Normal operation must not perform:

- coordinate transformation;
- gesture interpretation;
- key remapping;
- selective per-code transformation.

Any future suppression behavior, such as pause support, remains session policy
and must operate on complete interaction semantics rather than turning the
project into a general event-filtering framework.

## Synchronization recovery

`SYN_DROPPED` indicates that the consumer has lost synchronization with the
source event stream.

When libevdev reports synchronization-required state, the proxy session must
enter libevdev synchronization recovery and forward the recovered state stream
to the virtual device in order.

Synchronization recovery is part of runtime event policy.

It must not be implemented independently by the source or virtual-device
modules.

Source disconnection during synchronization recovery is handled through the same
source-loss lifecycle as disconnection during normal forwarding.

## Runtime lifecycle

The current runtime lifecycle is conceptually:

```text
STARTING
    |
    v
WAITING_FOR_SOURCE
    |
    | source available
    v
CREATING_PROXY
    |
    v
ACTIVE
    |
    | source lost
    v
SOURCE_LOST
    |
    | neutralize virtual state
    | release source
    v
WAITING_FOR_SOURCE
```

Shutdown may occur from any runtime state:

```text
any state
    |
    | SIGINT / SIGTERM
    v
SHUTTING_DOWN
    |
    v
EXIT
```

An unrecoverable failure transitions to cleanup and process exit.

The implementation does not require a generalized state-machine framework, but
lifecycle transitions must remain explicit and understandable.

## Future pause and runtime control

Future pause/resume support extends the proxy-session lifecycle.

The relevant conceptual states are:

```text
ACTIVE
PAUSING
PAUSED
RESUMING
```

Pause changes event delivery, not source consumption.

While paused:

- the physical source remains open when available;
- the source event queue continues to be drained;
- suppressed events are not replayed;
- the persistent virtual device remains present.

A pause request received during an active interaction must not leave the virtual
device in partially active state.

Likewise, resume must occur only at a safe input boundary so that the wake
interaction itself is not leaked to the virtual device.

The requested pause state must survive temporary source loss and reconnect.

## Future control service

External runtime control belongs in a control-service component.

The preferred transport is system D-Bus.

Its responsibility is to translate external requests into proxy-session state
requests and expose session state back to external clients.

The control service must not:

- read source events directly;
- write virtual-device events directly;
- own source or virtual-device lifecycle;
- decide independently when a pause/resume transition is complete;
- manage displays or backlights.

The proxy session remains authoritative.

Future control operations are expected to include equivalents of:

```text
Pause()
Resume()
SetPaused(bool)
```

and state such as:

```text
State
Paused
SourceAvailable
VirtualDeviceAvailable
```

Paused-source activity may produce a coalesced activity notification.

That notification is a wake signal, not a second raw-event transport.

## Device discovery

Device discovery supports operator-facing diagnostics and future installation
work.

It is responsible for:

- enumerating physical input candidates;
- identifying relevant event nodes;
- collecting concise device identity;
- classifying device type;
- excluding virtual uinput devices where reliably possible.

Discovery must remain read-only.

It must not select a runtime source automatically during normal `run`
operation.

The one-process-per-source model remains unchanged.

## Device inspection

Device inspection provides detailed information about one candidate source.

It may inspect:

- evdev identity;
- capabilities;
- udev properties;
- libinput-related properties;
- effective source permissions;
- `/dev/uinput` accessibility;
- proxy-readiness conditions.

Inspection may produce recommendations.

For example, if the physical source is visible to libinput and
`LIBINPUT_IGNORE_DEVICE=1` is not configured, inspection may suggest an
appropriate udev rule.

Any generated rule must be based on device properties specific enough to avoid
carelessly matching unrelated devices.

If the tool cannot confidently generate a suitably narrow rule, it should report
that limitation rather than emit a misleading rule.

Inspection remains strictly non-destructive.

Actual installation or persistent configuration belongs to future installation
functionality.

## Runtime permissions

The runtime does not inherently require root.

It requires:

- sufficient access to read the configured physical evdev source;
- sufficient access to `/dev/uinput`.

On Raspberry Pi OS, the intended deployment model uses the existing `input`
group where appropriate.

`/dev/uinput` may require a static-node udev rule such as:

```udev
KERNEL=="uinput", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"
```

Permission provisioning is external deployment configuration.

The runtime must not depend on:

- sudoers configuration;
- setuid-root execution;
- running the long-lived proxy process as root.

Future installation functionality may configure permissions explicitly.

## Logging architecture

The runtime emits diagnostic output through standard output and standard error.

The detailed logging policy is defined in `docs/ENGINEERING.md`.

Architecturally:

- logging reports lifecycle and operational state;
- logging must not become a second raw-event stream;
- runtime behaviour must not depend on systemd or journald;
- diagnostics should remain useful both interactively and under a service
  manager.

## Runtime control architecture

Version 0.3 adds runtime observation and control without changing the fundamental
one-process-per-source proxy model.

Runtime control is a control-plane concern. It must remain architecturally
separate from the evdev-to-uinput data path.

The runtime data path remains:

```text
physical evdev source
        |
        v
   proxy session
        |
        v
virtual uinput device
```

Runtime observation and control follow a separate path:

```text
external client
        |
        v
   system D-Bus
        |
        v
input-proxy instance
        |
        v
   proxy session
```

The proxy session remains authoritative for runtime behaviour and state.

### Design principles

Each running proxy instance MUST remain independently operable.

One `input-proxy run` process continues to own:

- one configured physical source;
- one configured virtual-device name;
- one proxy session;
- one virtual uinput device when source state permits;
- one runtime-control endpoint when D-Bus is available.

No `input-proxy` process manages another `input-proxy` process.

Version 0.3 MUST NOT introduce a central runtime manager, registry daemon, or
supervisor.

The system D-Bus daemon provides the directory of currently connected runtime
instances. Clients discover running proxies through D-Bus rather than through a
separate `input-proxy` registry.

### Ownership boundaries

Runtime responsibilities are deliberately separated.

`input-proxy` owns:

- proxy-session state;
- source and virtual-device lifecycle;
- pause and resume semantics;
- runtime activity detection;
- runtime properties exposed to clients.

D-Bus owns:

- inter-process communication;
- service-name ownership;
- runtime discovery transport;
- method dispatch;
- property-change delivery.

The process launcher owns process lifetime.

This may be:

- an interactive shell;
- systemd;
- another process supervisor.

D-Bus MUST NOT become the process-lifecycle authority.

Runtime control therefore MUST NOT provide a method that terminates or restarts
the proxy process.

Future systemd integration remains responsible for service start, stop, restart,
enablement, and persistence.

### Decentralized instance discovery

Every `input-proxy` runtime process independently connects to the system D-Bus
when possible.

Each connected process owns one well-known D-Bus service name within the
`input-proxy` namespace.

The initial naming model is:

```text
net.controlforge.InputProxy1.Instance.p<PID>
```

For example:

```text
net.controlforge.InputProxy1.Instance.p1842
```

The process identifier is used only as an ephemeral machine-safe D-Bus address.

Clients MUST NOT treat the D-Bus service name as the persistent logical identity
of a proxy instance.

Clients discover running instances by:

1. enumerating names currently owned on the system bus;
2. selecting names within the `net.controlforge.InputProxy1.Instance.` namespace;
3. querying the public instance properties from each matching service.

No additional registry process is required.

If a proxy process terminates, its D-Bus connection and service-name ownership
disappear automatically.

### Runtime identities

Three different identities serve different purposes and MUST remain distinct.

#### Logical runtime identity

The configured virtual-device name is the operator-facing logical identity.

Example:

```text
HDMI-A-1-Touch
```

This identity:

- is configured through `--name`;
- appears in operator-facing output and logs;
- is unique among simultaneously running `input-proxy` processes;
- is enforced independently of D-Bus.

The existing race-safe runtime name-ownership mechanism remains authoritative
for this uniqueness requirement.

#### D-Bus identity

The D-Bus service name is an IPC address.

Example:

```text
net.controlforge.InputProxy1.Instance.p1842
```

It is:

- machine-generated;
- valid only for the current process lifetime;
- not intended for persistence;
- not intended for operator configuration.

Clients that need a specific logical instance SHOULD discover current services
and match the `DeviceName` property.

#### Deployment identity

Future persistent deployments may assign a systemd instance identity.

For example:

```text
input-proxy@hdmi-a-1-touch.service
```

This identity belongs to deployment configuration and may remain stable across
boots.

The systemd instance identity MUST NOT be assumed to equal either the configured
`DeviceName` or the D-Bus service name.

### D-Bus availability

D-Bus runtime control is optional.

Failure to connect to the system D-Bus or acquire the runtime service name MUST
NOT prevent the proxy from operating.

When D-Bus initialization fails:

- `input-proxy run` MUST log a clear startup warning;
- normal evdev-to-uinput proxy operation MUST continue;
- pause/resume control through D-Bus is unavailable;
- D-Bus activity tracking is disabled.

The core proxy runtime MUST remain usable without D-Bus.

D-Bus augments the runtime; it does not become a prerequisite for forwarding
input.

### D-Bus object model

Each connected runtime process exports one instance object at a fixed object
path:

```text
/net/controlforge/InputProxy1/Instance
```

The initial project-specific interface is:

```text
net.controlforge.InputProxy1.Instance
```

The interface version is part of the public API namespace.

Each process exports only its own runtime instance.

No process exports objects representing other proxies.

### Public runtime properties

The initial runtime-control interface exposes the following read-only
properties:

```text
DeviceName             string
Source                 string
Version                string
PID                    uint32
Paused                 boolean
SourceAvailable        boolean
ActivityWhileRunning   boolean
ActivityWhilePaused    boolean
```

`DeviceName`, `Source`, `Version`, and `PID` are stable for the life of the
runtime process.

`Paused`, `SourceAvailable`, `ActivityWhileRunning`, and
`ActivityWhilePaused` may change while the process is running.

Changes to observable properties SHOULD use the standard
`org.freedesktop.DBus.Properties.PropertiesChanged` mechanism rather than
project-specific property-change signals.

A generalized runtime-state enumeration is intentionally not exposed initially.

Independent runtime conditions SHOULD be represented directly rather than
combined into an ambiguous state value.

### Runtime control methods

The initial project-specific control surface contains only:

```text
Pause()
Resume()
```

Both methods MUST be idempotent.

Calling `Pause()` while already paused MUST succeed without repeating
neutralization unnecessarily.

Calling `Resume()` while already forwarding MUST succeed without altering
runtime behaviour unnecessarily.

A toggle-style method MUST NOT be provided.

Clients must be able to request a desired state deterministically.

Process termination is outside the runtime-control interface and MUST NOT be
implemented as a D-Bus method.

### Standard D-Bus interfaces

The runtime object SHOULD support applicable standard D-Bus interfaces rather
than duplicating their functionality.

These include:

```text
org.freedesktop.DBus.Properties
org.freedesktop.DBus.Introspectable
org.freedesktop.DBus.Peer
```

The standard peer `Ping()` method SHOULD provide end-to-end communication
verification.

A project-specific `Ping()` method is unnecessary unless a future requirement
demonstrates otherwise.

### Pause semantics

Pause changes event delivery, not source ownership.

When pause is requested:

1. the proxy session safely neutralizes active virtual input state using the
   same correctness principles used for source-loss neutralization;
2. the physical source remains open when available;
3. source events continue to be consumed;
4. source events are not forwarded to the virtual device;
5. suppressed events are not replayed later;
6. the persistent virtual device remains present.

Pause state MUST survive temporary source loss and reconnect.

If the source is unavailable while the proxy is paused, the instance remains
logically paused when the source returns.

### Resume semantics

Resume MUST restore forwarding from the current physical source state rather
than replaying activity that occurred while paused.

The implementation SHOULD synchronize relevant stateful virtual controls to the
source's current state before normal forwarding resumes.

Relevant state may include:

- current `EV_KEY` state;
- current `EV_SW` state;
- current absolute-axis state;
- current Type-B multitouch slot/contact state.

Relative motion and other transient events that occurred during the pause are
discarded.

The synchronization sequence SHOULD end with an appropriate synchronization
boundary before normal event forwarding resumes.

Resume MUST NOT require the physical device to reach an assumed all-neutral
state.

This avoids indefinite waiting for valid devices that expose persistent physical
switches as continuously active input controls.

### Runtime activity tracking

Runtime activity tracking is available only when D-Bus integration is active.

The runtime exposes two independent boolean properties:

```text
ActivityWhileRunning
ActivityWhilePaused
```

These properties report source-device activity, not virtual-device output.

Raw input events MUST NOT be transported through D-Bus.

Unless future hardware validation demonstrates a need for narrower filtering,
source activity is defined as any physical source event other than `EV_SYN`.

#### Activity while running

`ActivityWhileRunning` represents recent source activity while forwarding is
enabled.

When meaningful source activity is detected:

```text
ActivityWhileRunning = true
```

The property remains true for a retriggerable hold interval.

Every additional activity event while forwarding is enabled restarts the hold
timer.

After the configured interval expires without further activity:

```text
ActivityWhileRunning = false
```

The runtime option controlling this interval is:

```text
--activity-timeout-ms VALUE
```

The default is:

```text
5000 ms
```

The accepted range MUST be bounded to sane values. Exact minimum and maximum
values belong to the D-Bus/runtime-control interface specification.

#### Activity while paused

`ActivityWhilePaused` provides a throttled activity indication while input
forwarding is paused.

When meaningful source activity is first detected while paused:

```text
ActivityWhilePaused = true
```

The property remains true for a short throttle interval and then returns to
false.

Additional activity during the active throttle interval does not repeatedly
extend the interval.

After the property returns to false, later activity may assert it again.

This produces a debounced/coalesced indication suitable for clients that use
paused input activity as a wake or attention trigger without exposing raw input
events.

The runtime option controlling this interval is:

```text
--detection-throttle-ms VALUE
```

The default is:

```text
250 ms
```

The accepted range MUST be bounded to sane values. Exact minimum and maximum
values belong to the D-Bus/runtime-control interface specification.

The two activity properties are mutually exclusive in normal operation:

- while forwarding, `ActivityWhilePaused` remains false;
- while paused, `ActivityWhileRunning` remains false.

### Device discovery integration

Version 0.3 extends operator tooling with awareness of running runtime instances.

`input-proxy list` continues to enumerate physical source candidates.

When D-Bus is available, it also discovers currently running `input-proxy`
instances and presents their relevant runtime mappings.

If D-Bus is available but no runtime instances are found, `list` SHOULD
explicitly report that no proxies are currently running.

If D-Bus is unavailable, physical-device discovery MUST still succeed and the
command SHOULD report that runtime-instance information is unavailable.

### Device inspection integration

`input-proxy inspect PATH` continues to perform physical-device inspection
independently of D-Bus.

When D-Bus is available, inspection additionally discovers running
`input-proxy` instances whose configured source corresponds to the inspected
source.

Multiple running instances may refer to the same physical source and MUST be
reported when present.

If D-Bus is available and no matching runtime instances exist, inspection
SHOULD remain silent rather than emitting a "none found" section.

If D-Bus is unavailable, physical-device inspection MUST still succeed and the
command SHOULD report that runtime-instance information could not be queried.

### Future systemd compatibility

Version 0.3 runtime architecture MUST remain compatible with a future systemd
template-unit deployment model.

The intended Version 0.4 relationship is:

```text
input-proxy@instance.service
        |
        v
one input-proxy run process
        |
        v
one independent D-Bus runtime endpoint
```

A future systemd service instance supervises exactly one `input-proxy` process.

No future service-management requirement should require replacing the
decentralized Version 0.3 runtime-control model with a central orchestrator.

Manual execution and systemd execution MUST expose the same runtime-control
behaviour.

### Implementation boundary

The public runtime-control architecture does not prescribe the internal
event-loop mechanism used for D-Bus integration.

The implementation MAY integrate D-Bus file descriptors into the existing
runtime loop or use another suitable mechanism.

The initial implementation preference is `sd-bus` through `libsystemd`, subject
to implementation validation.

Low-level implementation choices MUST NOT leak into the public D-Bus contract.

### Runtime-control non-goals

Runtime control does not turn `input-proxy` into:

- a process supervisor;
- a service manager;
- a central proxy orchestrator;
- a multi-source aggregator;
- an input remapper;
- a gesture recognizer;
- a coordinate transformer;
- a raw input-event transport.

Process supervision belongs to the process launcher.

Persistent deployment belongs to future systemd integration.

The proxy session remains focused on faithfully managing one physical source and
one logical virtual input device.

## Installation boundary

Future installation functionality may modify persistent system configuration.

That functionality must remain separate from runtime and read-only diagnostic
modes.

The installer may manage artifacts such as:

- udev rules;
- `/dev/uinput` permissions;
- persistent source mappings;
- systemd units;
- D-Bus policy/service files.

Normal runtime and inspection code must not silently perform these changes.

## Architectural invariants

The following invariants define the core design:

- one runtime proxy session manages one physical source;
- each configured virtual-device name has at most one running proxy-session
  owner;
- the physical source is recoverable;
- the virtual device is the stable logical device;
- temporary compatible source loss does not remove the virtual device;
- source-loss neutralization uses kernel-visible virtual state;
- every forwarded event passes through proxy-session policy;
- low-level device modules report mechanics, while the session owns lifecycle
  policy;
- normal runtime does not modify persistent system configuration;
- diagnostic modes are read-only;
- the project does not perform general-purpose input remapping;
- future external control requests session transitions rather than manipulating
  device resources directly.
