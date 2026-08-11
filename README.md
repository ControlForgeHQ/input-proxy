# input-proxy

`input-proxy` is a small Linux utility that republishes a physical evdev input
device as a virtual uinput device with a configurable name.

It is intended for situations where otherwise identical input devices need to
be distinguished by software that cannot identify them using stable hardware
paths.

```text
physical evdev device
    -> input-proxy
    -> virtual uinput device
    -> libinput or another input consumer
```

The proxy preserves the source device's capabilities and forwards its event
stream without semantic remapping.

The process remains running when its configured source device is unavailable or
disconnected. It waits for the device to appear, forwards events while the
source is present, and automatically resumes operation when the source returns.

## Status

Version **0.1.0** provides:

- faithful evdev-to-uinput event forwarding;
- automatic source disconnect and reconnect handling;
- persistent virtual-device lifetime across compatible reconnects;
- capability-aware virtual-device replacement;
- `SYN_DROPPED` synchronization recovery;
- virtual-device neutralization after source loss;
- graceful `SIGINT` / `SIGTERM` shutdown;
- concise lifecycle logging with optional verbose diagnostics.

Future development is described in `docs/ROADMAP.md`.

## Building

Building requires a C17 compiler, CMake, pkg-config, and the libevdev
development package.

On Debian and Ubuntu:

```sh
sudo apt install build-essential cmake pkg-config libevdev-dev
```

Configure and build:

```sh
cmake -S . -B build
cmake --build build
```

Run the regression tests:

```sh
ctest --test-dir build --output-on-failure
```

## Usage

```sh
./build/input-proxy \
    --source /dev/input/event0 \
    --name "Touchscreen Proxy"
```

Additional commands:

```sh
./build/input-proxy --help
./build/input-proxy --version
```

Use `--verbose` to explain internal runtime decisions and recovery behavior.
Normal output remains focused on concise operational lifecycle events. Neither
mode logs raw input events.

## Permissions

`input-proxy` does not need to run as root, but the running user must be able to
read the physical evdev source and access `/dev/uinput`.

On Raspberry Pi OS, the normal user is typically a member of the `input` group,
and physical input devices are normally already accessible through that group.

Check your current configuration:

```bash
id
ls -l /dev/input/event0
ls -l /dev/uinput
```

`input-proxy inspect PATH` reports these runtime access checks and, when the
observed ownership, mode, group membership, or module state supports a safe
next step, collects concise remediation after the readiness result. Inspection
only reports suggested commands; it never runs them or changes system
configuration.

A typical physical source:

```text
crw-rw---- root input ... /dev/input/event0
```

If `/dev/uinput` is instead:

```text
crw------- root root ... /dev/uinput
```

create:

```text
/etc/udev/rules.d/70-input-proxy-uinput.rules
```

containing:

```udev
KERNEL=="uinput", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"
```

Then reboot and verify:

```bash
ls -l /dev/uinput
```

Expected:

```text
crw-rw---- root input ... /dev/uinput
```

Once both the physical source and `/dev/uinput` are accessible through the
`input` group, `input-proxy` can be run normally without `sudo`.

## License

Released under the MIT License.
