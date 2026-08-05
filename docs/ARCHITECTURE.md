# Architecture

`input-proxy` is intentionally built from a small number of independent components.

## High-level flow

```text
           evdev                    uinput

Physical Device
      │
      ▼
 Device Monitor
      │
      ▼
 Device Proxy
      │
      ▼
 Virtual Device
      │
      ▼
 libinput / compositor / application
```

The application should remain small and modular.

Each component should have one primary responsibility.

---

## Device Monitor

Responsible for:

- waiting for the configured source device
- detecting device appearance
- detecting device removal
- opening and closing the source device

The monitor should know nothing about uinput.

---

## Device Proxy

Responsible for:

- inspecting the source device
- reproducing supported capabilities
- creating the virtual device
- forwarding events
- handling synchronization (`SYN_DROPPED`)
- destroying the virtual device

The proxy should not know how the source device was discovered.

---

## Virtual Device

Represents the uinput device.

Responsible for:

- virtual device creation
- identity (name, phys)
- capability registration
- event injection
- cleanup

---

## Main

Responsible only for:

- parsing configuration
- logging
- application lifetime
- coordinating the monitor and proxy

Business logic should live elsewhere.

---

## Application state machine

The application lifecycle should be represented explicitly as a state machine rather than as deeply nested retry and forwarding loops.

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

### State responsibilities

#### STARTING

- validate application configuration;
- initialize process-level resources;
- verify that required facilities such as uinput are accessible;
- install signal handling.

Configuration errors and permanently unavailable required resources are fatal at this stage.

#### WAITING_FOR_SOURCE

- wait efficiently for the configured source path to become available;
- remain responsive to shutdown requests;
- avoid busy-waiting;
- avoid repeatedly logging the same missing-device message.

A missing source is a normal operating condition, not an error.

#### CREATING_PROXY

- open the source device;
- initialize its libevdev representation;
- inspect its identity and capabilities;
- create the corresponding virtual uinput device.

If the source disappears during creation, clean up partial resources and return to `WAITING_FOR_SOURCE`.

A device that exists but is permanently incompatible may be treated as an error, provided the diagnostic clearly explains the incompatibility.

#### PROXYING

- forward events from the source to the virtual device;
- preserve event ordering and synchronization boundaries;
- recover from `SYN_DROPPED`;
- detect source removal or loss;
- remain responsive to shutdown requests.

#### CLEANING_UP

- destroy the virtual device;
- close the physical source;
- release all per-device resources;
- reset state required before reconnecting.

Cleanup must be safe when initialization completed only partially.

#### SHUTTING_DOWN

- stop waiting for or reading the source;
- destroy any active virtual device;
- close all resources;
- exit cleanly.

### Transition discipline

State transitions should be explicit and logged at an appropriate verbosity.

Avoid hiding lifecycle transitions inside unrelated helper functions. Helpers should return enough information for the application coordinator to decide the next state.

The initial implementation does not require a general-purpose state-machine framework. A straightforward enum and transition loop are preferred.

---

## Non-goals

The application deliberately does not understand:

- Wayland
- DRM/KMS
- displays
- monitors
- windows
- touch mapping
- calibration
- gestures
- coordinate transforms

Those responsibilities belong to software above this layer.

---

## One process per device

Each process proxies exactly one source device.

Running multiple proxy instances is the responsibility of the service manager (typically systemd).

This keeps the implementation simple, robust, and fault-isolated.