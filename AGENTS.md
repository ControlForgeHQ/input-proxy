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

The proxy preserves the source device's capabilities and forwards its event
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

## Command-line interface

```text
input-proxy --source PATH --name NAME [--verbose]
input-proxy --help
input-proxy --version
```

The kernel and udev determine the virtual device's `/dev/input/eventN` path.
The application must not accept or promise a specific destination event path.

## Version 0.1 scope

Version 0.1 provides:

- waiting for the configured source device when it is unavailable;
- automatic source detection and reconnect;
- capability-preserving virtual-device creation;
- persistent virtual-device lifetime across compatible reconnects;
- capability-aware virtual-device replacement when required;
- faithful event forwarding;
- preservation of `EV_SYN` report boundaries;
- `SYN_DROPPED` synchronization recovery;
- source-loss virtual-device neutralization;
- graceful `SIGINT` and `SIGTERM` shutdown;
- concise lifecycle logging;
- optional verbose lifecycle diagnostics.

Touchscreens and multitouch devices remain important validation cases, but the
implementation must remain generic.

## Device lifecycle

The application is intended to remain running even when its source device is
temporarily unavailable.

Its Version 0.1 lifecycle is:

```text
wait for source
    -> source appears
    -> open source
    -> create persistent virtual device
    -> forward events
    -> source disappears
    -> neutralize virtual device
    -> close source
    -> wait for source
    -> source returns
    -> compatible?
           yes -> reuse existing virtual device
           no  -> replace virtual device
    -> resume forwarding
```

A missing or disconnected source device is not a fatal condition.

Hotplug reconnect may include a short period in which the source device node
exists before udev has finished applying its final permissions.

A permission-denied result during the first source acquisition is fatal.

After a source has previously been opened successfully, a permission-denied
result during reconnect may be retried for a bounded settling period before it
is considered fatal.

Do not treat permission denial as globally equivalent to source absence, and do
not retry persistent permission failures forever.

The virtual device should remain present across compatible source reconnects.

If the capabilities of the returning source differ from those of the existing
virtual device, the virtual device should be replaced automatically.

The implementation should use an efficient event-driven mechanism where
practical and must not busy-wait. A simple bounded retry loop is acceptable when
it is easy to understand, sleeps between attempts, and avoids repeated logging
while waiting.

The process should terminate only when:

- it receives `SIGINT` or `SIGTERM`;
- its command-line configuration is invalid;
- a required system resource cannot be initialized;
- an unrecoverable runtime error occurs.

## Logging

Runtime logging follows the project logging policy documented in
`docs/ARCHITECTURE.md`.

In summary:

- normal lifecycle and status messages are written to standard output;
- warnings and errors are written to standard error;
- logging occurs on meaningful lifecycle transitions rather than retry loops;
- raw evdev events must not be logged during normal operation;
- `--verbose` adds lifecycle and diagnostic context rather than event dumps.

## Runtime permissions

`input-proxy` is intended to run without root privileges.

The runtime user must have permission to:

- read the configured physical evdev source;
- access `/dev/uinput`.

On Raspberry Pi OS, this is typically achieved using the existing `input` group.

The application itself must not depend on:

- sudoers configuration;
- a setuid executable;
- execution as root.

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