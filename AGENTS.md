# AGENTS.md

## Project purpose

`input-proxy` is a small Linux utility that republishes one physical evdev input
device as a virtual uinput device with a configurable name.

The intended data path is:

```text
physical evdev device
    -> input-proxy
    -> virtual uinput device
    -> libinput or another input consumer
```

The proxy should preserve the source device's capabilities and forward its event
stream without semantic remapping.

Future pause functionality may intentionally suppress complete interactions
under proxy-session control. This is a lifecycle feature, not a general-purpose
event-filtering or remapping system.

## Core design constraints

- Use C17.
- Use libevdev and uinput.
- Run one proxy process per source device.
- Keep the core application independent of Wayland, DRM, compositors, display
  management, systemd configuration, and udev configuration.
- Do not perform coordinate transformation, gesture interpretation, key
  remapping, or selective per-code event filtering.
- Do not add a general-purpose event-filtering language.
- All source events must pass through proxy-session policy before virtual-device
  injection.
- The source-device module must not write directly to the virtual device.
- The virtual-device module must not read directly from the source.
- Normal proxy mode must not automatically discover or select devices.
- Normal proxy mode must never modify system configuration.
- Installation functionality may modify system configuration only when
  explicitly invoked.
- Prefer small, explicit, maintainable code over abstraction or feature breadth.
- Avoid unrelated refactoring.
- Do not expand project scope without an explicit issue or instruction.
- Do not implement future roadmap features early unless the issue explicitly
  includes them.

## Initial command-line interface

```text
input-proxy --source PATH --name NAME [--verbose]
input-proxy --help
input-proxy --version
```

The kernel and udev determine the virtual device's `/dev/input/eventN` path. The
application must not accept or promise a specific destination event path.

## Initial scope

The first release should:

- wait for the configured source path if the device is not currently present;
- detect when the configured source device becomes available;
- open one evdev source;
- inspect and reproduce its supported capabilities;
- create one virtual uinput device;
- assign the configured name;
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

Touchscreens and multitouch devices are important validation cases, but the core
implementation must remain generic.

## Device lifecycle

The application is intended to remain running even when its source device is
temporarily unavailable.

Its initial normal lifecycle is:

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

The implementation should use an efficient event-driven mechanism where
practical and must not busy-wait. A simple bounded retry loop may be used
initially if it is easy to understand, logs sensibly, and sleeps between
attempts.

The process should terminate only when:

- it receives SIGINT or SIGTERM;
- its command-line configuration is invalid;
- a required system resource such as uinput is permanently unavailable;
- another unrecoverable initialization error occurs.

## Future pause and control architecture

Version 0.3 introduces pause and activity control as specified in
`docs/ROADMAP.md` and `docs/ARCHITECTURE.md`.

When working on that functionality:

- pausing must keep the source open and the virtual device present;
- source events must continue to be consumed while paused;
- suppressed events must not be replayed;
- pausing and resuming must occur only at safe interaction boundaries;
- a pause request must not leave a forwarded key, button, or touch contact
  stuck;
- the interaction that triggers a wake notification must remain suppressed;
- D-Bus handlers must request proxy-session transitions rather than directly
  manipulating device resources;
- display and backlight management remain outside this repository;
- control and activity notifications must not become a second raw-event
  transport.

Do not implement these Version 0.3 behaviours as part of an earlier issue unless
the issue explicitly requests them.

## Out of scope

- Wayland protocol integration
- DRM or KMS access
- display discovery or backlight control
- coordinate calibration or transformation
- gesture recognition
- general-purpose event remapping or filtering
- multiple source devices in one process
- internal daemonization
- automatic udev or systemd configuration during normal proxy operation
- graphical configuration
- network transport
- bundled third-party dependencies