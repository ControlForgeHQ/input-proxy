#ifndef INPUT_PROXY_SOURCE_DEVICE_INTERNAL_H
#define INPUT_PROXY_SOURCE_DEVICE_INTERNAL_H

#include <libevdev/libevdev.h>

struct input_proxy_source_device;

const struct libevdev *input_proxy_source_device_get_libevdev(
    const struct input_proxy_source_device *device
);

#endif
