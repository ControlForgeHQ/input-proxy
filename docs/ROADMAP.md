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
  - examples;
  - troubleshooting guidance.
- Treat the Version 0.1 runtime syntax as superseded by:

```text id="gvw85a"
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

```text id="ckg0ev"
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

```text id="u2a88v"
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

# Version 0.3.0 — Runtime control and activity notifications

## Goal

Allow external software to safely control runtime event delivery while
preserving source consumption, virtual-device stability, and clean interaction
boundaries.

## Required runtime behaviour

Version 0.3 must:

- provide an explicit paused operating state;
- support requesting pause and resume;
- support starting in the paused state;
- continue reading and processing the source while forwarding is suppressed;
- keep the persistent virtual device present while paused;
- preserve requested pause state across source disconnect and reconnect;
- remain externally controllable while the source is unavailable;
- suppress paused events rather than queueing or replaying them;
- complete pause and resume transitions only at safe interaction boundaries;
- avoid leaving keys, buttons, touch contacts, or multitouch contacts stuck;
- report lifecycle and pause-state changes externally;
- emit a coalesced activity notification while paused;
- keep the interaction that triggered wake activity suppressed;
- resume forwarding only after that interaction has completed;
- remain responsive to shutdown in every lifecycle state.

## Pause semantics

A pause request received during an active interaction must not immediately cut
off forwarding in a way that leaves active virtual state behind.

The session should transition through a pausing state until the current
interaction reaches a clean boundary.

Once fully paused:

- the physical source remains open when available;
- input continues to be consumed;
- events are discarded rather than queued;
- the virtual device remains present.

## Resume semantics

If the source is neutral when resume is requested, forwarding may resume
immediately.

If a wake interaction is still active, the session should remain in a resuming
state until that interaction completes.

The next new interaction may then be forwarded normally.

## Activity notification

While paused, meaningful source activity should generate a coalesced external
notification.

The activity interface is a wake trigger, not a second event stream.

It must not mirror raw evdev traffic.

## Control interface

The preferred control transport is the local system D-Bus.

The control interface should support operations equivalent to:

```text id="nyo0zs"
Pause()
Resume()
SetPaused(bool paused)
```

It should expose state equivalent to:

```text id="c07k0y"
State
Paused
SourceAvailable
VirtualDeviceAvailable
```

It should also provide activity and relevant state-change notifications.

The proxy session remains authoritative for deciding when requested transitions
are safe to complete.

## Runtime integration

- Keep the runtime single-threaded unless a demonstrated requirement justifies
  otherwise.
- Integrate source input, runtime control, lifecycle transitions, and shutdown
  through one explicitly coordinated execution model.
- Do not allow control callbacks to mutate device ownership independently.

## Validation

Version 0.3 should include:

- pause-state transition tests;
- event-suppression tests;
- no-replay tests;
- pause-during-active-interaction tests;
- resume-during-wake-interaction tests;
- activity-notification coalescing tests;
- disconnect/reconnect while paused;
- control while the source is unavailable;
- persistent virtual-device validation while paused;
- representative real-touchscreen validation.

## Non-goals

Version 0.3 does not:

- manage displays or backlights;
- install services or udev rules;
- add general event remapping;
- expose raw input events over D-Bus;
- introduce network control.

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

Add:

```text id="t9txl2"
input-proxy install
```

The interactive installer should:

- enumerate available input devices;
- allow source selection;
- optionally identify a device through observed activity;
- propose or request a unique virtual-device name;
- configure a stable logical instance identity;
- allow the instance to start active or paused;
- create the required persistent source mapping;
- configure source permissions where required;
- configure `/dev/uinput` access where required;
- configure the systemd service instance;
- configure D-Bus support;
- reload affected system services where required;
- enable and start the configured instance;
- report all persistent changes before applying them.

## Non-interactive installation

Support a scriptable form where practical, for example:

```text id="jwyf9m"
input-proxy install \
    --source PATH \
    --name NAME
```

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

External software may use future runtime-control interfaces for display
management or automation, but `input-proxy` itself remains focused on Linux
input proxying.