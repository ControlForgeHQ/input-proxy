# input-proxy

`input-proxy` is a small Linux utility that republishes a physical evdev input device as a virtual uinput device with a configurable identity.

It is intended for situations where otherwise identical input devices need to be distinguished by software that cannot identify them using stable hardware paths.

```text
physical evdev device
    -> input-proxy
    -> virtual uinput device
    -> libinput or another input consumer
```

The proxy preserves the source device’s capabilities and forwards its event stream without semantic remapping.

The process remains running when its configured source device is unavailable or disconnected. It waits for the device to appear, creates the corresponding virtual device, forwards events while the source is present, and returns to waiting if the source disappears.

## Status

This project is in its initial design and implementation phase.

## Building

Building requires a C17 compiler, CMake, pkg-config, and the libevdev development
package. On Debian and Ubuntu, install the dependencies with:

```sh
sudo apt install build-essential cmake pkg-config libevdev-dev
```

Configure and build the project with:

```sh
cmake -S . -B build
cmake --build build
```

Run the hardware-independent tests with:

```sh
ctest --test-dir build --output-on-failure
```
