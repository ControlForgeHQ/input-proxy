#ifndef INPUT_PROXY_VIRTUAL_DEVICE_H
#define INPUT_PROXY_VIRTUAL_DEVICE_H

struct input_proxy_source_device;
struct input_proxy_virtual_device;

int input_proxy_virtual_device_create(
    struct input_proxy_virtual_device **device,
    const struct input_proxy_source_device *source_device,
    const char *device_name,
    const char *physical_identifier
);

void input_proxy_virtual_device_destroy(
    struct input_proxy_virtual_device *device
);

#endif