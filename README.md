# input-proxy

`input-proxy` is a Linux utility that creates a controllable intermediary between
a physical input device and the software that consumes its input. It reads a
physical evdev device, reproduces its capabilities and events through a virtual
uinput device, and gives that virtual device a configured name.

```text
Physical evdev device
        │
        ▼
   input-proxy
        │
        ▼
Virtual uinput device
        │
        ▼
libinput, compositor, or another input consumer
```

This is useful whenever input needs a stable logical identity or a deliberate
routing point that the physical device alone does not provide.

The configured proxy name becomes that stable logical identity. In Version 0.3
this evolves into the validated **Instance Name**, which also becomes the
runtime-control identity and the basis for future persistent deployment.

`input-proxy` does **not** remap events. It faithfully forwards the source event
stream while preserving the source device's supported capabilities. It performs
no coordinate transformation, gesture interpretation, or per-code filtering.

---

## Project status

The latest released version is **0.2.0**.

The repository is currently preparing the documented Version 0.3 runtime-control
and observability contract for implementation. Features described for Version
0.3 and later are design targets and are not available unless the README
explicitly identifies them as implemented.

- [Version 0.2 capabilities](#version-02-capabilities)
- [Planned roadmap](docs/ROADMAP.md)
- [Version 0.3 D-Bus interface design](docs/DBUS_INTERFACE.md)

---

## Why it exists

`input-proxy` was originally developed for a small Linux kiosk system driving
multiple displays with multiple touchscreens.

In a deliberately minimal compositor or kiosk environment, two otherwise similar
physical touch devices may not expose enough convenient identity for the desired
touchscreen-to-output routing. Rather than allowing downstream software to
consume the physical devices directly, `input-proxy` creates stable, named
virtual devices that can be targeted deliberately by compositor or application
configuration.

Linux input stacks and full desktop environments can support touchscreen mapping
in several ways. This example is **not** a claim that Linux generally cannot map
multiple touchscreens. It is simply one concrete situation where an explicit
intermediary provides additional flexibility and control.

Touchscreens are also not a special limitation of the project. `input-proxy`
proxies one physical evdev source per process and generically reproduces
supported evdev capabilities, making the same physical-to-virtual model
applicable to keyboards, mice, button panels, jog wheels, HID controllers, and
other Linux input devices.

---

## Operator workflow

The normal workflow is intentionally simple:

1. Discover a physical device.
2. Inspect its capabilities and deployment readiness.
3. Start a proxy for the selected source.

### 1. Discover source devices

```sh
./build/input-proxy list
```

`list` displays a concise table of physical input candidates, including each
event path, device type, bus, and friendly name. Virtual uinput devices are
excluded where they can be identified reliably.

### 2. Inspect a candidate

```sh
./build/input-proxy inspect /dev/input/event0
```

`inspect` reports:

- device identity and capabilities;
- runtime accessibility;
- relevant udev and libinput properties;
- an overall proxy-readiness assessment.

When the observed system state supports a safe recommendation, `inspect` also
provides actionable remediation guidance. If the device is ready to proxy, it
prints a suggested `run` command that can usually be copied directly.

Inspection is strictly read-only. It never creates a proxy or modifies system
configuration.

Paths such as `/dev/input/event0` may change across boots or reconnections.
Whenever `inspect` reports a matching `/dev/input/by-id/...` or
`/dev/input/by-path/...` entry, prefer that persistent path for long-running
configurations.

### 3. Run the proxy

Prefer a persistent source path when one is available:

```sh
./build/input-proxy run --source /dev/input/by-path/platform-example-event --name "Touchscreen Proxy"
```

Otherwise use the event path reported by `list`:

```sh
./build/input-proxy run --source /dev/input/event0 --name "Touchscreen Proxy"
```

The configured name becomes the virtual device name and must be unique among
simultaneously running `input-proxy` processes. Beginning with Version 0.3, this
validated name also becomes the canonical runtime Instance Name used throughout
the project.

Add `--verbose` to include detailed lifecycle diagnostics such as source
identity, reconnect handling, compatibility decisions, synchronization recovery,
and shutdown behavior. Neither normal nor verbose mode emits raw input events.

Command-specific help is available through:

```sh
./build/input-proxy --help
./build/input-proxy list --help
./build/input-proxy inspect --help
./build/input-proxy run --help
./build/input-proxy --version
```

---

## Version 0.2 capabilities

Version 0.2 provides:

- concise physical-device discovery;
- comprehensive read-only device inspection and deployment diagnostics;
- actionable runtime-permission and libinput guidance;
- faithful evdev-to-uinput event forwarding;
- configurable virtual-device naming;
- automatic source disconnect and reconnect handling;
- persistent virtual-device lifetime across compatible reconnects;
- capability-aware virtual-device replacement;
- synchronization recovery and safe neutralization after source loss;
- concise runtime logging with optional verbose diagnostics; and
- graceful `SIGINT` and `SIGTERM` shutdown.

If a configured source is temporarily unavailable, the proxy waits for it.
When a connected source disappears, `input-proxy` safely releases active virtual
state, retains a compatible virtual device where possible, and resumes
forwarding automatically when the source returns.

---

## Building and testing

Building requires:

- a C17 compiler;
- CMake;
- pkg-config; and
- the libevdev development package.

On Debian or Ubuntu:

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

---

## Permissions

`input-proxy` does not inherently require root privileges.

The running user must be able to:

- read the selected physical event device; and
- write to `/dev/uinput`.

Device ownership and permissions vary by distribution and deployment.

Always begin troubleshooting with:

```sh
./build/input-proxy inspect /dev/input/event0
```

`inspect` distinguishes source-access and uinput-access problems, reports
overall proxy readiness, and provides actionable recommendations whenever the
observed system state supports a safe recommendation. It can also detect when a
physical device remains visible to libinput and duplicate physical and proxied
input is likely.

Version 0.2 diagnoses and advises. It does not automatically modify permissions,
install udev rules, or change system configuration.

---

## Not yet available

Version 0.2 intentionally focuses on the core proxy runtime and operator
experience.

The following capabilities are planned for future releases:

- runtime D-Bus status and control;
- pause and resume;
- persistent service installation;
- systemd unit creation;
- automatic udev-rule installation;
- packaging;
- configuration files; and
- general-purpose input remapping.

Runtime awareness and control are the documented design target for Version 0.3.
Persistent installation and service integration are planned for Version 0.4.

For additional project information see:

- [Documentation index](docs/README.md)
- [Architecture](docs/ARCHITECTURE.md)
- [D-Bus Interface](docs/DBUS_INTERFACE.md)
- [Roadmap](docs/ROADMAP.md)
- [Engineering Principles](docs/ENGINEERING.md)

---

## License

Released under the [MIT License](LICENSE).
