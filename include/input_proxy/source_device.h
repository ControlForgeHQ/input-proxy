#ifndef INPUT_PROXY_SOURCE_DEVICE_H
#define INPUT_PROXY_SOURCE_DEVICE_H

struct input_proxy_source_device;

int input_proxy_source_device_open(
    struct input_proxy_source_device **device,
    const char *source_path
);

void input_proxy_source_device_close(
    struct input_proxy_source_device *device
);

#endif