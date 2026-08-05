# Architecture

`input-proxy` is intentionally built around a single primary abstraction: the proxy session.

Each process manages exactly one proxy session.

## High-level flow

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
    +-- Session Lifecycle
```

The external event path is:

```text
physical evdev device
    -> input-proxy
    -> virtual uinput device
    -> libinput or another input consumer
```

## Proxy session

A proxy session owns everything associated with proxying one physical input device.

It is responsible for:

- the configured source path;
- the configured virtual device identity;
- the current lifecycle state;
- the physical source device;
- the virtual uinput device;
- event forwarding;
- source disconnect and reconnect handling;
- synchronization recovery;
- orderly cleanup and shutdown.

The proxy session coordinates lower-level device helpers but should not contain platform configuration or installation logic.

## Source device

The source-device component represents the physical evdev device.

It is responsible for:

- opening and closing the configured source path;
- initializing the libevdev device;
- inspecting identity and capabilities;
- reading events;
- detecting device loss;
- supporting synchronization recovery after `SYN_DROPPED`.

It should not know how the virtual device is created or how the session lifecycle is coordinated.

## Virtual device

The virtual-device component represents the uinput device.

It is responsible for:

- creating the virtual device;
- assigning its configured identity;
- reproducing supported source capabilities;
- injecting forwarded events;
- destroying the virtual device;
- safely cleaning up partially initialized resources.

It should not discover or open physical source devices.

## Main application

The main application is responsible only for:

- parsing command-line arguments;
- selecting the requested operating mode;
- initializing process-level resources;
- installing signal handling;
- constructing and running a proxy session;
- returning an appropriate process exit status.

Normal proxy behaviour should live in the proxy-session implementation rather than in `main.c`.

Future installation functionality should remain a separate top-level operating mode:

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

The installer must not be coupled to the runtime event-forwarding implementation.

## Session lifecycle

The proxy session lifecycle should be represented explicitly rather than implemented through deeply nested retry and forwarding loops.

Initial states:

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
    | proxy created successfully
    v
PROXYING
    |
    | source is disconnected
    v
CLEANING_UP
    |
    v
WAITING_FOR_SOURCE
```

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

### STARTING

- validate session configuration;
- initialize process-level resources;
- verify that required facilities such as uinput are accessible;
- install signal handling.

Configuration errors and permanently unavailable required resources are fatal at this stage.

### WAITING_FOR_SOURCE

- wait efficiently for the configured source path to become available;
- remain responsive to shutdown requests;
- avoid busy-waiting;
- avoid repeatedly logging the same missing-device message.

A missing source is a normal operating condition, not an error.

### CREATING_PROXY

- open the source device;
- initialize its libevdev representation;
- inspect its identity and capabilities;
- create the corresponding virtual uinput device.

If the source disappears during creation, release partial resources and return to `WAITING_FOR_SOURCE`.

A device that exists but is permanently incompatible may be treated as an error, provided the diagnostic clearly explains the incompatibility.

### PROXYING

- forward events from the source to the virtual device;
- preserve event ordering and synchronization boundaries;
- recover from `SYN_DROPPED`;
- detect source removal or loss;
- remain responsive to shutdown requests.

### CLEANING_UP

- destroy the virtual device;
- close the physical source;
- release all per-device resources;
- reset session state before reconnecting.

Cleanup must be safe when initialization completed only partially.

### SHUTTING_DOWN

- stop waiting for or reading the source;
- destroy any active virtual device;
- close all resources;
- exit cleanly.

## Transition discipline

Lifecycle transitions should be explicit and logged at an appropriate verbosity.

Lower-level helpers should return enough information for the proxy session to decide the next state. They should not silently perform session-level transitions.

The initial implementation does not require a general-purpose state-machine framework. A straightforward state enum and transition loop are preferred.

## One process per device

Each process manages exactly one proxy session and therefore proxies exactly one source device.

Multiple devices are handled through multiple process instances, typically managed by systemd.

This keeps runtime behaviour:

- simple;
- fault-isolated;
- independently restartable;
- easy to diagnose.

The internal session abstraction should remain self-contained, but this does not imply future support for multiple sessions inside one process.

## Platform constraints

Some Linux evdev/uinput behaviours are constrained by the kernel interface rather than the implementation.

Examples include:

- The virtual device physical path (`Phys`) cannot be configured through the standard uinput interface.


## Non-goals

The runtime proxy deliberately does not understand:

- Wayland;
- DRM or KMS;
- displays or monitors;
- windows;
- touch mapping;
- calibration;
- gestures;
- coordinate transformations;
- systemd configuration;
- udev rule generation.

Those responsibilities belong to software above or alongside the runtime proxy.