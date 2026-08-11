# input-proxy

`input-proxy` is a Linux utility that places a controllable intermediary between
a physical input device and the software that consumes its input. It reads a
physical evdev device, republishes its capabilities and events through a virtual
uinput device, and gives that virtual device a configured name.

```text
physical evdev device
    -> input-proxy
    -> virtual uinput device
    -> libinput or another input consumer
```

This is useful when input needs a stable logical identity or a deliberate
routing point that the physical device alone does not provide. `input-proxy`
does not remap events: it preserves the source capabilities and forwards the
event stream without coordinate transformation, gesture interpretation, or
per-code filtering.

## Why it exists

The project was motivated by a small Linux system with multiple displays and
multiple touchscreens. In a minimal compositor or kiosk environment, two
otherwise similar touch devices may not expose enough convenient identity for
the desired touchscreen-to-output routing. A proxy can give each physical
source a distinct virtual-device name, allowing downstream configuration to
target the stable logical devices.

Linux input stacks and full desktop environments can support touchscreen
mapping in several ways; this example is not a claim that Linux generally
cannot map multiple touchscreens. It is one concrete case where an explicit
intermediary is useful, especially in a deliberately minimal environment.

Touchscreens are also not a special limitation of the proxy. It works with one
physical evdev source per process and generically reproduces supported evdev
capabilities, so other input-device types can use the same physical-to-virtual
model.

## Version 0.2 capabilities

The Version 0.2 command set provides:

- concise physical-device discovery;
- detailed, read-only device and deployment diagnostics;
- actionable permission and libinput-configuration guidance where the device
  can be identified safely;
- faithful evdev-to-uinput event forwarding;
- a configurable, uniquely owned virtual-device name;
- automatic source disconnect and reconnect handling;
- a persistent virtual-device lifetime across compatible reconnects;
- capability-aware virtual-device replacement;
- synchronization recovery and safe neutralization after source loss;
- concise runtime logging with optional verbose lifecycle diagnostics; and
- graceful `SIGINT` and `SIGTERM` shutdown.

The proxy can wait for a configured source that is temporarily unavailable. If
a connected source disappears, it safely releases active virtual state, keeps a
compatible virtual device present, and resumes forwarding when the source
returns.

## Building and testing

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

## Operator workflow

The normal workflow is to discover a device, inspect it, and then run a proxy
for it.

### 1. Discover source devices

```sh
./build/input-proxy list
```

`list` shows a concise table of physical input candidates, including each event
path, device type, bus, and friendly name. Virtual uinput devices are excluded
where they can be identified reliably.

### 2. Inspect a candidate

Use the event path reported by `list`:

```sh
./build/input-proxy inspect /dev/input/event0
```

`inspect` reports the device identity and capabilities, source and `/dev/uinput`
access, relevant udev and libinput properties, and an overall proxy-readiness
result. When the observed state supports a safe recommendation, it also prints
specific remediation and a suggested `run` command. Inspection is read-only: it
does not create a proxy or change permissions, udev rules, or other system
configuration.

Paths such as `/dev/input/event0` can change across boots or reconnections. When
`inspect` reports a matching persistent `/dev/input/by-id/...` or
`/dev/input/by-path/...` path, prefer that path for long-running use. The proxy
reopens the exact configured source path after a disconnect, so a persistent
path helps it find the intended physical device again.

### 3. Run the proxy

```sh
./build/input-proxy run \
    --source /dev/input/by-path/platform-example-event \
    --name "Touchscreen Proxy"
```

If no suitable persistent link is available, use the event path from `list`:

```sh
./build/input-proxy run \
    --source /dev/input/event0 \
    --name "Touchscreen Proxy"
```

The name identifies the virtual device and must be unique among simultaneously
running `input-proxy` processes. Add `--verbose` for source identity,
reconnection, compatibility, synchronization-recovery, and lifecycle detail.
Normal and verbose modes deliberately do not dump raw input events.

Command-specific and global help are available with:

```sh
./build/input-proxy --help
./build/input-proxy run --help
./build/input-proxy list --help
./build/input-proxy inspect --help
./build/input-proxy --version
```

## Permissions

`input-proxy` does not inherently need root. The running user must be able to
read the selected physical event device and write to `/dev/uinput`.
Device-node ownership and permissions vary by distribution and deployment.

Start troubleshooting with the same user that will run the proxy:

```sh
./build/input-proxy inspect /dev/input/event0
```

The inspection result distinguishes source-access and uinput-access problems
and prints actionable suggestions when it can do so safely. It can also warn
when a physical source remains visible to libinput and duplicate physical and
proxied input is likely. The Version 0.2 commands diagnose and advise; they do
not apply system changes automatically.

On systems that grant input-device access through an `input` group, the user
running the proxy may need membership in that group and `/dev/uinput` may need
an appropriate udev rule. Follow the device-specific output from `inspect`
rather than assuming one permission setup fits every system.

## Not yet available

Version 0.2 does not provide runtime D-Bus control, pause and resume, persistent
service installation, systemd-unit creation, udev-rule installation, packaging,
configuration files, or general-purpose input remapping. Runtime awareness and
control are planned for Version 0.3, while persistent installation and service
integration are planned for Version 0.4.

See the [roadmap](docs/ROADMAP.md) for planned releases, the
[architecture](docs/ARCHITECTURE.md) for design and lifecycle details, and the
[documentation index](docs/README.md) for the rest of the project documentation.

## License

Released under the [MIT License](LICENSE).
