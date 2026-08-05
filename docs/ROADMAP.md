# Roadmap

This document describes the planned evolution of `input-proxy`.

It is intentionally conservative. Features listed in future versions should not be implemented early unless specifically requested.

---

# Version 0.1

Goal:

A small, robust, transparent Linux evdev-to-uinput proxy.

Supports:

- one source device per process
- configurable virtual device identity
- automatic source disconnect/reconnect
- generic evdev capability cloning
- transparent event forwarding
- clean shutdown
- concise logging

Does not support:

- event transformation
- event filtering
- coordinate scaling
- gesture recognition
- multiple source devices
- configuration files
- plugins
- networking

The primary objective is correctness and reliability.

---

# Version 0.2

Goal:

Improve diagnostics and operational visibility without changing the core proxy model.

Potential enhancements:

- richer device identity and capability reporting;
- improved lifecycle and reconnect logging;
- additional integration and hardware tests;
- clearer diagnostics for unsupported capabilities;
- optional machine-readable inspection output where useful.

Version 0.2 must not introduce configuration files or change the one-process-per-device model.

---

# Version 0.3

Goal:

Provide straightforward Debian-family packaging and guided system installation.

Required features:

- build a distributable Debian package;
- support installation through `dpkg`;
- support clean package removal;
- install the application binary, documentation, systemd template units, and required support files;
- provide an interactive installation workflow through:

```text
input-proxy --install
```

The interactive installer should:

- allow the user to select a currently available input device;
- optionally identify a device by waiting for input activity;
- request or propose a unique virtual device name;
- create the required persistent udev source rule;
- create or configure the required systemd instance;
- reload udev and systemd configuration;
- enable and start the configured proxy instance;
- clearly report every persistent system change before applying it;
- provide actionable errors when installation cannot be completed.

The installer should also support a non-interactive form:

```text
input-proxy \
    --source PATH \
    --name NAME \
    --install
```

Additional non-interactive options may be introduced when required to remove ambiguity, but existing proxy-mode command-line behaviour must remain compatible.

Installation responsibilities must remain separate from normal proxy operation. Running `input-proxy` without `--install` must never modify system configuration.

The installation workflow may require elevated privileges for specific system changes. Privileged operations must be explicit and narrowly scoped.

Package removal must not silently delete locally generated proxy-instance configuration. Removal and purge behaviour should follow normal Debian conventions:

- package removal may retain local configuration;
- package purge may remove package-managed configuration;
- generated local device mappings should be handled conservatively and documented clearly.

No general-purpose configuration-file format is planned. Any files generated for systemd or udev are deployment artifacts, not an application configuration interface.

# Explicit non-goals

The following are outside the intended scope of this project:

- compositor replacement
- display management
- Wayland protocol implementation
- DRM/KMS management
- calibration
- gesture interpretation
- input remapping language
- macro recording
- automation
- GUI configuration
- network input transport

These are valuable problems, but they belong in separate projects.
