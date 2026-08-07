# input-proxy

`input-proxy` is a small Linux utility that republishes a physical evdev input device as a virtual uinput device with a configurable name.

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

## Permissions

`input-proxy` does not need to run as root, but the running user must be able to
read the physical evdev source and access `/dev/uinput`.

On Raspberry Pi OS, the normal user is typically a member of the `input` group,
and physical input devices are normally already accessible through that group.

Check your current configuration with:

```bash
id
ls -l /dev/input/event0
ls -l /dev/uinput
```

A typical physical source looks like:

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

The expected result is:

```text
crw-rw---- root input ... /dev/uinput
```

Once both the physical source and `/dev/uinput` are accessible through the
`input` group, run `input-proxy` normally without `sudo`.
