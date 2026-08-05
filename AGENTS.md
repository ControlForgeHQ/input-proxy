# AGENTS.md

## Project purpose

`input-proxy` is a small Linux utility that republishes one physical evdev input device as a virtual uinput device with a configurable identity.

The intended data path is:

```text
physical evdev device
    -> input-proxy
    -> virtual uinput device
    -> libinput or another input consumer
```

The proxy should preserve the source device’s capabilities and forward its event stream without semantic remapping.

## Core design constraints

- Use C17.
- Use libevdev and uinput.
- Run one proxy process per source device.
- Keep the core application independent of Wayland, DRM, compositors, systemd, and udev configuration.
- Do not perform coordinate transformation, gesture interpretation, key remapping, or event filtering.
- Do not automatically discover devices.
- Do not modify system configuration.
- Prefer small, explicit, maintainable code over abstraction or feature breadth.
- Avoid unrelated refactoring.
- Do not expand project scope without an explicit issue or instruction.

## Initial command-line interface

```text
input-proxy --source PATH --name NAME [--phys IDENTIFIER] [--verbose]
input-proxy --help
input-proxy --version
```

The kernel and udev determine the virtual device’s `/dev/input/eventN` path. The application must not accept or promise a specific destination event path.

## Initial scope

The first release should:

- wait for the configured source path if the device is not currently present;
- detect when the configured source device becomes available;
- open one evdev source;
- inspect and reproduce its supported capabilities;
- create one virtual uinput device;
- assign the configured name and optional physical identifier;
- forward events faithfully;
- preserve `EV_SYN` report boundaries;
- handle `SYN_DROPPED`;
- detect source-device disconnection without terminating;
- destroy the corresponding virtual device after source disconnection;
- return to waiting for the configured source path;
- recreate the virtual device and resume forwarding when the source returns;
- handle SIGINT and SIGTERM cleanly;
- report useful lifecycle information and errors to stderr;
- exit nonzero only for unrecoverable configuration or initialization failures.

Touchscreens and multitouch devices are important validation cases, but the core implementation must remain generic.

## Device lifecycle

The application is intended to remain running even when its source device is temporarily unavailable.

Its normal lifecycle is:

```text
wait for source
    -> source appears
    -> open and validate source
    -> create virtual device
    -> forward events
    -> source disappears
    -> destroy virtual device
    -> wait for source
```

A missing or disconnected source device is not a fatal condition.

The implementation should use an efficient event-driven mechanism where practical and must not busy-wait. A simple bounded retry loop may be used initially if it is easy to understand, logs sensibly, and sleeps between attempts.

The process should terminate only when:

- it receives SIGINT or SIGTERM;
- its command-line configuration is invalid;
- a required system resource such as uinput is permanently unavailable;
- another unrecoverable initialization error occurs.

## Out of scope

- Wayland protocol integration
- DRM or KMS access
- display discovery
- coordinate calibration or transformation
- event remapping or filtering
- multiple source devices in one process
- internal daemonization
- automatic udev or systemd configuration
- graphical configuration
- network transport
- bundled third-party dependencies