#ifndef INPUT_PROXY_DEVICE_INSPECTION_INTERNAL_H
#define INPUT_PROXY_DEVICE_INSPECTION_INTERNAL_H

#include <input_proxy/result.h>

#include <stdio.h>

enum input_proxy_result input_proxy_inspect_device(
    FILE *stream,
    FILE *error_stream,
    const char *device_path,
    const char *sysfs_input_path,
    const char *uinput_path,
    const char *udev_data_path
);

#endif
