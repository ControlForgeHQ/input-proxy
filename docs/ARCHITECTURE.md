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

Runtime observation and control follow a separate control path:

```text
external client
    -> system D-Bus
    -> input-proxy instance
    -> proxy session
```

Data flow, diagnostics, runtime policy, and external control must remain
architecturally distinct.

### Terminology

- **Proxy Session**: the runtime policy and lifecycle authority for one logical
  proxy instance.
- **Physical Source**: the configured evdev device that supplies input events.
- **Virtual Device**: the persistent uinput device presented to input consumers.
- **Instance Name**: the validated, operator-supplied identity of one logical
  proxy instance.
- **Source Availability**: the current ability of the proxy session to acquire
  and communicate with its configured physical source.
- **Runtime Control**: observation and state requests delivered through the
  optional D-Bus control plane.
- **Runtime Activity**: a coalesced indication of recent physical-source input,
  not a transport for raw input events.

## Command-line architecture

`input-proxy` uses a subcommand-oriented CLI.

The primary command structure is:

```text
input-proxy run --source PATH --name INSTANCE_NAME [--verbose]

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

`--name` specifies the Instance Name. The Instance Name is also used as the
virtual uinput device name and identifies the instance throughout runtime
control, diagnostics, and future persistent deployment.

Before runtime startup output or device lifecycle begins, the proxy session
acquires exclusive ownership of the configured Instance Name. Ownership is
represented by a bound Linux abstract Unix-domain socket whose address contains
a fixed application namespace followed by the exact configured name.

The atomic socket bind makes concurrent acquisition race-safe. The session holds
the socket for its lifetime, and closing the descriptor on cleanup—or kernel
cleanup after abnormal process termination—immediately releases the name.

This ownership namespace is specific to `input-proxy` runtime processes. Linux
input devices with the same displayed evdev name do not participate in it.

When D-Bus runtime control is available, successful acquisition of the
corresponding well-known D-Bus service name provides an additional assertion of
the same Instance Name ownership. The abstract socket remains the runtime's
authoritative Instance Name uniqueness mechanism, while D-Bus exposes that
identity to external clients.

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
- configured Instance Name;
- current physical source device;
- persistent virtual device;
- runtime lifecycle state;
- event-forwarding policy;
- reconnect policy;
- synchronization recovery;
- source-loss neutralization;
- pause and resume state;
- runtime activity state;
- shutdown handling.

Every source event must pass through session-level policy before being written to
the virtual device.

The source-device layer must not write directly to the virtual device.

The virtual-device layer must not independently read the physical source.

This boundary ensures that reconnect handling, synchronization recovery,
neutralization, pause/resume policy, and runtime control are applied
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

## State-correction failure

State correction includes:

- neutralization after source loss;
- neutralization when entering paused operation;
- synchronization before event forwarding begins.

Before event forwarding begins, the virtual device MUST be synchronized to the
physical source's current state.

This is a forwarding invariant rather than a condition associated with any
particular lifecycle transition. It applies whenever forwarding is about to
begin, including initial source acquisition, source reconnection, and resumption
after forwarding has been suppressed.

Selective neutralization requires complete kernel-visible virtual state for all
supported `EV_KEY` controls and, for Type-B multitouch devices, the tracking
identifier of every slot. A query result is incomplete when the virtual device
advertises a required capability but its current value cannot be obtained.

Synchronization requires complete current physical-source state for the
stateful controls defined by the synchronization semantics. Querying current
virtual state to avoid redundant synchronization events is optional. If that
comparison state is unavailable, the session may emit a complete source-state
synchronization instead.

The session MUST NOT treat unavailable or incomplete state as inactive or
neutral. It MUST NOT compensate by blindly resetting supported values, ending
unverified multitouch contacts, replaying stored events, or continuing with a
partially completed correction sequence.

The virtual device can no longer be trusted when:

- a mandatory state query fails or returns incomplete state;
- writing any required neutralization or synchronization event fails;
- the final `EV_SYN` / `SYN_REPORT` cannot be emitted;
- the physical source disappears during synchronization after the virtual
  device may have been partially changed.

Any such failure is fatal to the proxy session. The session MUST:

1. stop or withhold normal forwarding;
2. destroy the virtual device;
3. close the physical source if it remains open;
4. report a clear fatal diagnostic identifying the failed operation;
5. terminate with a failure result.

The session MUST NOT attempt in-place continuation or automatic virtual-device
replacement after a state-correction failure. Process cleanup releases Instance
Name ownership and any D-Bus service-name ownership normally. A process
supervisor or operator may subsequently start a new session from a clean
lifecycle boundary.

## Reconnect

After source loss, the session waits for the configured source to return.

When the source reappears:

1. reopen it;
2. initialize source state;
3. compare it with the existing virtual device;
4. retain or replace the virtual device as required;
5. continue according to the current event-forwarding policy.

If forwarding is to begin, the normal forwarding invariant applies: synchronize
the virtual device to current physical-source state before forwarding any source
events.

If forwarding remains suppressed, source events continue to be consumed
according to the current session policy without being forwarded.

A reconnect is a lifecycle transition, not process reinitialization..

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

## Lifecycle failure policy

The proxy session classifies failures according to the operation and lifecycle
context in which they occur. A low-level error is recoverable only when the
session has a defined transition that preserves its correctness invariants.
Errors must not be retried merely because they might be temporary.

The runtime applies the following policy:

| Originating operation | Result category | Session policy | Retry policy | Virtual-device lifetime | Diagnostic and process result |
|-----------------------|-----------------|----------------|--------------|-------------------------|-------------------------------|
| Initial source open or reacquisition | Source unavailable | Remain in `WAITING_FOR_SOURCE`. | Retry at the normal source-wait cadence until shutdown. | Retain an existing virtual device; none exists during initial acquisition. | Report the wait state once on standard output without log flooding; no process exit. |
| Initial source open | Permission denied | Treat as a configuration or access failure. | Do not retry. | No virtual device has been created. | Report the error on standard error and terminate with failure. |
| Source reacquisition | Permission denied | Allow the bounded reconnect-settling behavior defined above. | Retry only until the settling deadline. | Retain the existing neutralized virtual device during settling. | Use verbose lifecycle diagnostics during settling; persistent denial is reported on standard error and terminates with failure. |
| Initial source open or reacquisition | Generic open or source-initialization failure | Treat as fatal because the session has no defined evidence that retry is safe or useful. | Do not retry automatically. | Destroy any retained virtual device during cleanup. | Report the failed operation on standard error and terminate with failure. |
| Normal read or synchronization-recovery read | Source disconnected | Enter the source-loss lifecycle. Neutralize the virtual device, close the source, and wait for reacquisition. | Retry source acquisition at the normal source-wait cadence after successful neutralization. | Retain the neutralized virtual device. | Report the disconnect as a lifecycle event on standard output; no process exit. |
| Normal read or synchronization-recovery read | Generic read failure | Treat as fatal. A read failure that is not classified as source disconnection must not be converted into an indefinite reconnect loop. | Do not retry. | Destroy the virtual device and close the source during cleanup. | Report the failed read on standard error and terminate with failure. |
| Normal event forwarding | Virtual-device event-write failure | Treat as fatal because the kernel-visible virtual state may no longer match the forwarded source stream. | Do not retry the write or replace the virtual device in place. | Destroy the virtual device and close the source during cleanup. | Report the failed write on standard error and terminate with failure. |
| Initial virtual-device creation | Creation failure | Close the source and terminate. | Do not retry within the session. | Release any partially initialized virtual-device resources. | Report the creation failure on standard error and terminate with failure. |
| Reconnect compatibility comparison | Compatible source | Retain the existing virtual device and continue according to the current event-forwarding policy. | Not applicable. | Retain the existing virtual device. | Report normal reconnection; no process exit. |
| Reconnect compatibility comparison | Incompatible source | Perform the replacement transition defined below. | Make one replacement attempt for that reconnect transition. | Destroy the old virtual device before creating its replacement. | Successful replacement continues the session; replacement failure is reported on standard error and terminates with failure. |
| Neutralization or required state synchronization | State-correction failure | Apply the fail-closed policy defined in `State-correction failure`. | Do not retry or replace in place. | Destroy the virtual device and close the source. | Report a fatal diagnostic and terminate with failure. |
| Session setup or runtime policy | Invalid internal state, resource exhaustion, or another internal failure | Treat as fatal unless another section explicitly defines a recoverable transition. | Do not retry automatically. | Release all resources owned by the session. | Report the failure on standard error and terminate with failure. |

A fatal result always enters cleanup, releases Instance Name and D-Bus ownership,
destroys any virtual device, closes any physical source, and returns a nonzero
process exit status. A normal `SIGINT` or `SIGTERM` shutdown performs the same
resource cleanup but returns success.

Automatic restart after a fatal result belongs to an external supervisor or an
operator. The runtime must not introduce an unbounded retry loop for an error
category that this architecture classifies as fatal.

## Source compatibility after reconnect

A persistent virtual device may be retained only if it can correctly represent
the reconnected source. Compatibility means exact equality of the externally
relevant identity and event representation already exposed by the virtual
device. It is not a subset relationship or an estimate that two sources are
probably the same physical device.

The compatibility comparison is:

| Field class | Required comparison |
|-------------|---------------------|
| Bus type | Exact equality |
| Vendor identifier | Exact equality |
| Product identifier | Exact equality |
| Version identifier | Exact equality |
| Unique identifier | Exact nullable-string equality |
| Input properties | Exact set equality |
| Event types | Exact set equality |
| Event codes within every event type | Exact set equality |
| Absolute-axis minimum | Exact equality |
| Absolute-axis maximum | Exact equality |
| Absolute-axis fuzz | Exact equality |
| Absolute-axis flat | Exact equality |
| Absolute-axis resolution | Exact equality |
| Repeat delay and period | Exact equality |

Exact set equality means that both additions and removals are incompatible. A
virtual device with extra capabilities does not correctly represent a source
that no longer provides them.

Changes to bus type, vendor, product, version, or unique identifier require
replacement even when event capabilities are unchanged. Retaining the old
virtual device would expose stale source identity.

Compatibility deliberately ignores runtime state and metadata that is not part
of the represented virtual device:

| Field | Treatment |
|-------|-----------|
| Current `EV_KEY` values | Ignore; synchronize as runtime state before forwarding begins |
| Current `EV_SW` values | Ignore; synchronize as runtime state before forwarding begins |
| Current absolute-axis values | Ignore; synchronize as runtime state before forwarding begins |
| Current multitouch contacts and slot values | Ignore; synchronize as runtime state before forwarding begins |
| Source display name | Ignore; the virtual name is the configured Instance Name |
| Source physical-location string | Ignore; it is not part of the virtual representation |
| Configured source path | Ignore for compatibility; it selects the acquisition target |
| Event timestamps | Ignore; they are transient event data |
| Kernel-assigned event-node path | Ignore; it is a lifecycle artifact |

If the source is compatible:

```text
initialize source
    -> retain virtual device
    -> apply current event-forwarding policy
         |
         +-- forwarding remains suppressed
         |       -> consume source events without forwarding
         |
         +-- forwarding is to begin
                 -> synchronize current source state
                 -> begin forwarding only after synchronization succeeds
```

A compatible reconnect does not produce a virtual-device removal/addition
cycle.

If the source is incompatible:

```text
initialize source
    -> destroy old virtual device
    -> create replacement from new source
    -> apply current event-forwarding policy
         |
         +-- forwarding remains suppressed
         |       -> consume source events without forwarding
         |
         +-- forwarding is to begin
                 -> synchronize current source state
                 -> begin forwarding only after synchronization succeeds
```

Input consumers observe removal of the old virtual device and addition of the
replacement. The replacement retains the configured Instance Name and virtual
device name. The process, D-Bus endpoint, and configured source identity remain
the same logical proxy instance. Linux input and udev already expose the device
removal/addition cycle; the D-Bus interface does not duplicate it with a
project-specific replacement signal.

Identity or capability incompatibility is recoverable through replacement.

If the required replacement cannot be created, the session MUST close the
reconnected source and terminate with failure. It MUST NOT forward through the
incompatible old device, retain a source without a usable virtual
representation, or retry replacement indefinitely within the same lifecycle
transition.
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

Suppression behavior, including pause support, remains session policy
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

The resource lifecycle is conceptually:

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

Pause is an event-forwarding policy within the proxy session rather than a
separate resource lifecycle.

Conceptually:

```text
ACTIVE
  |
  | Pause()
  v
PAUSED
  |
  | Resume()
  v
ACTIVE
```

While paused, the session continues to consume an available Physical Source and
retains the Virtual Device. Source loss and reconnect continue to follow the
normal resource lifecycle shown above.

The requested pause state survives source loss. If the source returns while the
session is paused, the session remains paused until an explicit `Resume()`
request succeeds.

The implementation does not require a generalized state-machine framework, but
lifecycle and pause-state transitions must remain explicit and understandable.

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

## Runtime control

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
- one configured Instance Name;
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

System-bus policy controls which processes may own project service names and
which clients may access the exported interface. It is the sole authorization
boundary for D-Bus properties, standard interfaces, and methods. `input-proxy`
does not inspect caller credentials or apply a second service-level
authorization rule. A method call delivered by the bus is authorized for
session-level dispatch.

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

### Instance identity

Every running proxy has one stable Instance Name. It is supplied through
`--name`, validated before runtime startup, and MUST be unique among
simultaneously running proxy instances.

The Instance Name is the canonical identity of the proxy throughout the project.
Other runtime and deployment identifiers are derived from it rather than
introducing additional independent identities.

The same Instance Name identifies:

- the logical proxy instance;
- the virtual uinput device;
- the D-Bus runtime endpoint;
- operator-facing logs and diagnostics;
- the future systemd service instance.

For example:

```text
Instance Name:  HDMI-A-1-Touch
Virtual device: HDMI-A-1-Touch
D-Bus service:  net.controlforge.InputProxy1.Instance.HDMI-A-1-Touch
Future service: input-proxy@HDMI-A-1-Touch.service
```

One validation routine MUST establish that the supplied value is safe in every
downstream representation.

Exact Instance Name syntax and length constraints belong to
`docs/DBUS_INTERFACE.md` and the command-line contract.

### Decentralized discovery

Every runtime process independently connects to the system D-Bus when possible
and requests one well-known service name derived from its Instance Name:

```text
net.controlforge.InputProxy1.Instance.<InstanceName>
```

Clients that know an Instance Name can address it directly. Clients that need to
enumerate running proxies list currently owned names in the
`net.controlforge.InputProxy1.Instance.` namespace.

No additional registry process is required. If a process terminates, D-Bus
releases its name automatically. A restarted logical instance reclaims the same
well-known name, so its D-Bus address is stable across process lifetimes.

### D-Bus availability

D-Bus runtime control is optional.

Runtime identity and D-Bus initialization occur in this order:

1. validate the Instance Name;
2. acquire authoritative abstract-socket Instance Name ownership;
3. derive the D-Bus identifiers;
4. connect to the system bus;
5. export the runtime object and interfaces;
6. request the well-known service name without queueing;
7. mark D-Bus integration available.

D-Bus integration is all-or-nothing. It MUST NOT be reported as available until
every initialization step succeeds. Clients address only the well-known service
name; exporting an object under the connection's unique name is not a degraded
public control plane.

Failure to connect, export the complete interface, or acquire the runtime
service name MUST NOT prevent the proxy from operating.

When D-Bus initialization fails:

- `input-proxy run` MUST log a clear startup warning that identifies the failed
  stage and underlying platform reason where available;
- partial object exports, service-name ownership, connections, activity timers,
  and activity state MUST be cleaned up;
- normal evdev-to-uinput proxy operation MUST continue;
- pause/resume control through D-Bus is unavailable;
- D-Bus activity tracking is disabled.

The service-name request MUST use no-queue behavior. If the name is already
owned, the process disables D-Bus integration rather than waiting to acquire the
name later. Any future explicit reinitialization attempt repeats the complete
sequence and again requests the name without queueing.

Because authoritative abstract-socket ownership is acquired first, a D-Bus
service-name collision cannot be another conforming `input-proxy` process with
the same Instance Name. It indicates another name owner or an inconsistent
deployment and MUST be diagnosed as a D-Bus namespace conflict. The conflict
does not invalidate the process's authoritative Instance Name ownership and
does not prevent forwarding.

Rejection of a derived identifier after successful Instance Name validation is
an internal invariant violation, not an operator naming error. The diagnostic
MUST identify name derivation or validation as defective while the core proxy
continues without D-Bus integration.

If an established D-Bus connection is lost, the runtime MUST immediately mark
D-Bus integration unavailable, cancel activity timers, discard activity state,
stop exposing runtime control, report the loss without repeated log flooding,
and continue normal input forwarding. Automatic retry cadence is not an
architectural requirement.

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

### Public runtime interface

The D-Bus object exposes the public runtime contract for one proxy instance.

The interface provides read-only observation of instance identity, Source
Availability, pause state, and Runtime Activity. Independent runtime conditions
remain independent rather than being collapsed into an ambiguous generalized
state.

The control surface allows clients to request paused or forwarding operation.
Those requests MUST be deterministic and idempotent. A toggle-style operation is
not part of the architecture.

Process termination and restart remain outside the runtime-control interface.

Standard D-Bus interfaces provide properties, change notification,
introspection, and peer communication where applicable. The project-specific
interface must not duplicate those facilities.

The exact public service contract—including property names and types, methods,
errors, notifications, Instance Name validation limits, and client
expectations—is defined exclusively in `docs/DBUS_INTERFACE.md`.

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

Resume requests unpaused operation. It MUST restore forwarding from the current
physical source state rather than replaying activity that occurred while
paused.

Synchronizing the virtual device to current physical-source state is a mandatory
precondition whenever an unpaused session transitions from not forwarding to
forwarding. This includes:

- resuming with an available source;
- reacquiring a compatible source while unpaused;
- creating a replacement virtual device for an incompatible source while
  unpaused.

Required synchronization includes current state for supported:

- `EV_KEY` controls, including buttons and `BTN_TOUCH`;
- `EV_SW` controls;
- absolute axes;
- Type-B multitouch slots, tracking identifiers, and per-contact absolute
  values.

The session SHOULD emit only values required to make virtual state match current
source state. The synchronization sequence MUST form one coherent state update
and end with `EV_SYN` / `SYN_REPORT` before normal event forwarding begins.

Relative motion, miscellaneous events, synchronization events, historical
press/release sequences, and other transient activity that occurred while
forwarding was suppressed are discarded.

When resume is requested with an available source, the session remains paused
until required synchronization succeeds. If synchronization does not complete,
normal forwarding MUST NOT begin and the session MUST NOT report a successful
transition to unpaused operation.

When resume is requested without an available source, the session records
unpaused operation immediately. `Paused` becomes false while `SourceAvailable`
remains false, and no forwarding occurs. When the source returns, required
synchronization precedes forwarding.

Calling resume while the session is already unpaused is an idempotent no-op and
does not require another synchronization sequence.

When a source returns while the session remains paused, normal source
initialization and compatibility handling occur, but active source state MUST
NOT be synchronized into the virtual device. The session continues consuming
and suppressing source events until resume is requested.

Resume MUST NOT require the physical device to reach an assumed all-neutral
state.

This avoids indefinite waiting for valid devices that expose persistent physical
switches as continuously active input controls.

### Runtime activity tracking

Runtime activity is derived from physical-source input, not virtual-device
output. Raw input events MUST NOT be transported through D-Bus.

The runtime reports activity independently for forwarding and paused operation.
Running activity uses a retriggerable hold interval. Paused activity uses a
throttled indication suitable for wake or attention handling; events arriving
during the throttle interval do not continuously extend it.

These indications MUST remain mutually exclusive and are available only while
D-Bus integration is active. Exact property names, timing options, defaults,
bounds, and transition rules belong to `docs/DBUS_INTERFACE.md`.

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
input-proxy@<InstanceName>.service
        |
        v
one input-proxy run process
        |
        v
one independent D-Bus runtime endpoint
```

A future systemd service instance supervises exactly one `input-proxy` process.

The service instance uses the same validated Instance Name as the runtime and
D-Bus endpoint. Process lifetime remains owned by systemd or another launcher,
not by D-Bus.

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

Version 0.4 installation may modify persistent system configuration.

Installation remains architecturally separate from runtime and read-only
diagnostic modes.

The installer may manage artifacts such as:

- udev rules;
- `/dev/uinput` permissions;
- persistent source mappings;
- systemd service instances;
- D-Bus policy or service metadata.

Persistent installation is expected to derive the systemd service instance from
the same validated Instance Name used by the runtime and D-Bus endpoint.

Normal runtime and inspection code must not silently perform persistent system
changes.

Detailed installation workflow, release scope, and sequencing belong in
`docs/ROADMAP.md`.

## Architectural invariants

The following invariants define the core design:

- one runtime proxy session manages one physical source;
- each Instance Name has at most one running proxy-session owner;
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
- D-Bus runtime control is optional and never required for event forwarding;
- external runtime-control requests session transitions rather than manipulating
  device resources directly.
