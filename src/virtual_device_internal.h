#ifndef INPUT_PROXY_VIRTUAL_DEVICE_INTERNAL_H
#define INPUT_PROXY_VIRTUAL_DEVICE_INTERNAL_H

#include <libevdev/libevdev-uinput.h>

struct input_proxy_virtual_device;

struct libevdev_uinput *input_proxy_virtual_device_get_libevdev_uinput(
    struct input_proxy_virtual_device *device
);

#endif
