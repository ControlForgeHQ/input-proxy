#ifndef INPUT_PROXY_DEVICE_DISCOVERY_INTERNAL_H
#define INPUT_PROXY_DEVICE_DISCOVERY_INTERNAL_H

#include <input_proxy/result.h>

#include <stdio.h>

enum input_proxy_result input_proxy_list_devices(
    FILE *stream,
    const char *sysfs_input_path,
    const char *device_path
);

#endif
