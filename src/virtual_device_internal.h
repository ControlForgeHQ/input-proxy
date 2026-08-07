#ifndef INPUT_PROXY_VIRTUAL_DEVICE_INTERNAL_H
#define INPUT_PROXY_VIRTUAL_DEVICE_INTERNAL_H

#include <input_proxy/result.h>

#include <stdbool.h>

struct input_proxy_source_device;
struct input_proxy_virtual_device;
struct libevdev_uinput;

bool input_proxy_virtual_device_is_compatible(
    const struct input_proxy_virtual_device *device,
    const struct input_proxy_source_device *source_device
);

enum input_proxy_result input_proxy_virtual_device_neutralize(
    struct input_proxy_virtual_device *device
);

struct libevdev_uinput *input_proxy_virtual_device_get_libevdev_uinput(
    struct input_proxy_virtual_device *device
);

#endif
