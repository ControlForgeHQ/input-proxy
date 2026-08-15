# Roadmap

This document defines the planned evolution of `input-proxy`.

It is authoritative for release goals, milestone scope, feature sequencing, and
future product direction.

Implementation details belong in `docs/ARCHITECTURE.md`.

Engineering standards belong in `docs/ENGINEERING.md`.

Features listed in future versions should not be implemented early unless an
explicit issue or instruction requires them.

---

# Version 0.1.0 — Core runtime

## Goal

Provide a small, robust, transparent Linux evdev-to-uinput proxy with stable
logical device identity across normal source-loss conditions.

## Required features

- one source device per process;
- configurable virtual-device name;
- generic evdev capability cloning;
- transparent event forwarding;
- preservation of event ordering and synchronization boundaries;
- automatic source disconnect and reconnect handling;
- persistent virtual-device lifetime across compatible reconnects;
- safe neutralization of active virtual state after source loss;
- source compatibility validation after reconnect;
- virtual-device replacement when source capabilities are incompatible;
- `SYN_DROPPED` recovery;
- graceful `SIGINT` / `SIGTERM` shutdown;
- concise lifecycle logging;
- optional verbose lifecycle diagnostics;
- non-root runtime operation when device permissions permit.

## Non-goals

Version 0.1 does not provide:

- event transformation;
- selective event remapping or filtering;
- coordinate scaling;
- gesture recognition;
- multiple source devices in one process;
- device discovery commands;
- inspection tooling;
- runtime control interfaces;
- configuration files;
- installation automation;
- plugins;
- networking.

The primary objective is runtime correctness and reliability.

---

# Version 0.2.0 — Operator experience and diagnostics

## Goal

Make `input-proxy` easier to understand, diagnose, and operate without changing
its fundamental runtime proxy model.

Version 0.2 turns `input-proxy` from a single-purpose runtime executable into a
small operator-facing input-device utility.

## Command-line interface

- Convert the CLI to a subcommand-oriented design.
- Introduce:
  - `input-proxy run`
  - `input-proxy list`
  - `input-proxy inspect`
- Preserve global:
  - `--help`
  - `--version`
- Provide command-specific help.
- Improve help formatting with:
  - usage summaries;
  - option descriptions;
  - examples.
- Provide actionable troubleshooting guidance through `inspect` where the
  observed system state supports a safe recommendation.
- Treat the Version 0.1 runtime syntax as superseded by:

```text
input-proxy run --source PATH --name NAME [--verbose]
```

## Startup identity

- Print a concise startup header for runtime operation.
- Include:
  - application name;
  - version;
  - source repository.
- Make runtime invocation context easy to identify in interactive output and
  service logs.

## Device discovery

- Add:

```text
input-proxy list
```

- Present a concise human-readable list of available physical input devices.
- Show only the information needed to identify likely source candidates.
- Include:
  - event path;
  - friendly device name;
  - device classification;
  - compact hardware identity where useful.
- Exclude virtual uinput devices from the default list where they can be
  reliably identified.
- Avoid exhaustive capability dumps.

## Device inspection

- Add:

```text
input-proxy inspect PATH
```

- Provide detailed read-only inspection of one source candidate.
- Report where available:
  - device identity;
  - input classification;
  - bus/vendor/product/version information;
  - physical path and unique identity;
  - relevant capability summary;
  - multitouch characteristics;
  - source-device accessibility;
  - `/dev/uinput` accessibility;
  - relevant udev properties;
  - libinput-related configuration;
  - proxy-readiness status.

## Deployment diagnostics

- Detect whether the physical source is configured with
  `LIBINPUT_IGNORE_DEVICE=1`.
- Warn when the physical source remains visible to libinput in a deployment
  where duplicate physical/proxied input is likely.
- When safe and practical, suggest an appropriate device-specific udev rule.
- Do not automatically install or modify that rule.
- Do not emit an overly broad rule when the device cannot be identified
  narrowly enough.
- Keep `inspect` strictly read-only.

## Runtime diagnostics

- Improve contextual runtime error messages.
- Include useful source or virtual-device context where it helps identify the
  failed operation.
- Expand verbose diagnostics with:
  - source identity;
  - reconnect decisions;
  - capability compatibility decisions;
  - lifecycle transitions;
  - synchronization recovery context;
  - source-loss neutralization context.
- Continue to prohibit raw event dumps in normal and verbose runtime logging.
- Treat the configured virtual-device name as a unique logical instance
  identity among simultaneously running `input-proxy` processes.
- Refuse to start a runtime instance when another running `input-proxy` process
  already owns the same configured virtual-device name.
- Ensure duplicate-name detection is race-safe and remains valid for the
  lifetime of the running instance.
- Do not rely solely on scanning existing evdev device names to enforce
  uniqueness.

## Build and release improvements

- Make the CMake project version the single authoritative version source.
- Generate the public version header during CMake configuration.
- Eliminate manual duplication of the version number.
- Preserve the existing public version API.
- Ensure release preparation requires changing the version in one place only.

## Testing and validation

- Add regression coverage for the new command structure.
- Test:
  - top-level help;
  - command-specific help;
  - `--version`;
  - invalid command handling;
  - `list`;
  - `inspect`;
  - runtime invocation through `run`.
- Add hardware-independent tests for discovery and inspection where practical.
- Validate device discovery and inspection against representative real hardware.
- Preserve all Version 0.1 runtime behaviour.

## Non-goals

Version 0.2 does not introduce:

- automatic system configuration;
- udev-rule installation;
- systemd service creation;
- D-Bus runtime control;
- pause/resume behaviour;
- activity notifications;
- configuration files;
- JSON or other machine-readable output unless a concrete consumer requires it;
- raw event inspection;
- general-purpose input remapping.

Version 0.2 diagnoses and advises. It does not configure the machine.

---

# Version 0.3.0 — Runtime control and observability

## Goal

Introduce decentralized runtime observation and control while preserving the
existing one-process-per-source architecture and evdev-to-uinput data path.

Version 0.3 establishes the validated Instance Name as the stable logical
identity used by the virtual device and runtime-control interface.

## Runtime control

- Add optional system D-Bus integration.
- Keep each `input-proxy` runtime process independently operable.
- Register each running proxy as an independent D-Bus service.
- Derive the well-known D-Bus service name directly from the validated Instance
  Name.
- Do not introduce a central manager, registry daemon, or runtime supervisor.
- Expose the documented public D-Bus interface.
- Provide deterministic, idempotent:
  - `Pause()`
  - `Resume()`
- Use standard D-Bus interfaces where applicable.
- Continue normal evdev-to-uinput operation when D-Bus is unavailable.
- Report a startup warning when D-Bus runtime control cannot be initialized.

## Instance identity

- Treat `--name` as the operator-supplied Instance Name.
- Validate the Instance Name before runtime startup.
- Require the Instance Name to be unique among simultaneously running
  `input-proxy` instances.
- Use the same Instance Name for:
  - the logical proxy instance;
  - the virtual uinput device;
  - the D-Bus runtime endpoint;
  - operator-facing diagnostics and logging;
  - future persistent systemd deployment.
- Define exact Instance Name syntax and compatibility requirements in the public
  D-Bus interface specification.

## Runtime observability

- Expose runtime information through the public D-Bus interface.
- Include observation of:
  - Instance Name;
  - configured source;
  - runtime version;
  - process identifier;
  - pause state;
  - source availability;
  - activity while forwarding;
  - activity while paused.
- Use standard D-Bus property-change notification rather than project-specific
  state-change signals where practical.
- Do not expose an ambiguous generalized runtime-state value.

## Runtime activity

- Track physical-source activity when D-Bus runtime control is available.
- Provide separate activity indications for:
  - forwarding operation;
  - paused operation.
- Add configurable:
  - `--activity-timeout-ms`
  - `--detection-throttle-ms`
- Provide independent policy controlling whether motion counts as activity:
  - while running;
  - while paused.
- Use a retriggerable hold interval for activity while forwarding.
- Use a throttled indication for activity while paused.
- Do not transport raw input events through D-Bus.

## Pause and resume behaviour

- Keep the physical source open and consuming events while paused when the
  source is available.
- Neutralize active virtual input state when transitioning to paused operation.
- Suppress source events while paused without replaying them later.
- Preserve requested pause state across source loss and reconnect.
- On resume, synchronize relevant stateful virtual controls to the current
  physical source state before normal forwarding resumes.
- Do not require the physical source to reach an assumed all-neutral state
  before resuming.

## Device discovery

- Extend `input-proxy list` with awareness of running proxy instances.
- Discover running proxies through the system D-Bus when available.
- Display current source-to-instance mappings.
- Explicitly report when D-Bus is available but no proxy instances are running.
- Continue physical-device discovery when D-Bus is unavailable and report that
  runtime-instance information could not be queried.

## Device inspection

- Extend `input-proxy inspect PATH` with awareness of running proxy instances.
- Report any running proxy instances associated with the inspected physical
  source.
- Support multiple running proxies referring to the same physical source.
- Remain silent about runtime instances when D-Bus is available but no matching
  instances are found.
- Continue physical-device inspection when D-Bus is unavailable and report that
  runtime-instance information could not be queried.

## Documentation

- Publish the runtime-control architecture.
- Publish the public D-Bus interface specification.
- Document:
  - Instance Name requirements;
  - runtime discovery;
  - pause and resume behaviour;
  - activity semantics;
  - client expectations;
  - D-Bus compatibility requirements.

## Non-goals

Version 0.3 does not introduce:

- a central runtime manager or registry daemon;
- process start, stop, or restart control through D-Bus;
- persistent systemd service installation;
- packaging;
- automatic persistent system configuration;
- configuration files;
- raw input-event transport through D-Bus;
- general-purpose input remapping.

Version 0.3 adds a runtime control plane. Persistent deployment remains Version
0.4 work.

---

# Version 0.4.0 — Deployment and system integration

## Goal

Make installation, configuration, service deployment, and removal
straightforward on Debian-family systems while keeping runtime privileges
minimal.

## Packaging

- Build a distributable Debian package.
- Support installation through `dpkg`.
- Support normal package removal and purge semantics.
- Install:
  - application binary;
  - documentation;
  - systemd units;
  - udev rules;
  - D-Bus policy and service metadata;
  - other required deployment files.

## Interactive installation

- Add:

```text
input-proxy install
```

- Use the existing discovery and inspection capabilities to validate the
  requested deployment before persistent configuration is created.
- Enumerate available input devices.
- Allow source selection.
- Optionally identify a source through observed activity.
- Propose or request a validated Instance Name.
- Derive persistent runtime artifacts from that Instance Name.
- Allow the instance to start active or paused.
- Create the required persistent source mapping.
- Configure source permissions where required.
- Configure `/dev/uinput` access where required.
- Configure the corresponding systemd service instance.
- Configure required D-Bus policy or service metadata.
- Reload affected system services where required.
- Enable and start the configured instance.
- Report all persistent changes before applying them.
- Do not create a persistent service when inspection reports unresolved runtime
  readiness blockers.

## Non-interactive installation

Support a scriptable form where practical, for example:

```text
input-proxy install \
    --source PATH \
    --name INSTANCE_NAME
```

The supplied Instance Name must satisfy the same validation rules used by
`input-proxy run`.

Non-interactive installation must perform the same deployment-readiness
validation as interactive installation.

Additional explicit options may be added when needed to remove ambiguity.

## Privilege model

The long-running runtime process should remain unprivileged.

Installation may require elevated privileges for specific system changes.

Privileged operations must be explicit and narrowly scoped.

The installer should verify access to:

- the selected physical source;
- `/dev/uinput`.

On Raspberry Pi OS, the preferred default may use the existing `input` group.

The installer must not require:

- sudoers entries for normal runtime;
- a setuid-root executable;
- a permanently root-running proxy service.

## Removal behaviour

Package removal should follow Debian conventions.

Generated local proxy-instance configuration must be handled conservatively.

Normal removal may retain local configuration.

Purge may remove package-managed configuration where appropriate.

Locally generated device mappings should not be silently destroyed without clear
and documented semantics.

## Non-goals

Version 0.4 does not introduce:

- a graphical configuration interface;
- network management;
- general-purpose configuration files;
- display/backlight control;
- input remapping.

---

# Maintenance releases

Patch releases (`0.x.y`) contain:

- bug fixes;
- documentation corrections;
- validation improvements;
- narrowly scoped behavioural corrections.

Patch releases should not intentionally introduce new feature scope or major
architectural changes.

---

# Project-wide non-goals

The following remain outside the intended scope of `input-proxy`:

- compositor replacement;
- display or backlight management;
- Wayland protocol implementation;
- DRM or KMS management;
- calibration;
- coordinate transformation;
- gesture interpretation;
- general-purpose input remapping;
- macro recording;
- general automation;
- GUI configuration;
- network input transport.

External software may use the runtime-control interface for display management
or automation, but `input-proxy` itself remains focused on Linux input proxying.
