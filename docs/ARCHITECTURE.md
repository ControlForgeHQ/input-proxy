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
