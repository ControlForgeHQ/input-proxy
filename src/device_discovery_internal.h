#ifndef INPUT_PROXY_DEVICE_DISCOVERY_INTERNAL_H
#define INPUT_PROXY_DEVICE_DISCOVERY_INTERNAL_H

#include <input_proxy/result.h>

#include <stdio.h>
#include <stdbool.h>

struct input_proxy_device_identity {
    char name[256];
    const char *bus;
    const char *classification;
    bool virtual_device;
};

bool input_proxy_read_device_identity(
    const char *event_sysfs_path,
    struct input_proxy_device_identity *identity
);

enum input_proxy_result input_proxy_list_devices(
    FILE *stream,
    const char *sysfs_input_path,
    const char *device_path
);

#endif
