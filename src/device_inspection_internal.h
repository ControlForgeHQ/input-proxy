#ifndef INPUT_PROXY_DEVICE_INSPECTION_INTERNAL_H
#define INPUT_PROXY_DEVICE_INSPECTION_INTERNAL_H

#include <input_proxy/result.h>

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

struct input_proxy_access_remediation {
    bool source_ok;
    const char *source_group;
    bool source_group_readable;
    bool source_group_member;
    bool uinput_exists;
    bool uinput_ok;
    const char *uinput_group;
    bool uinput_group_writable;
    bool uinput_group_member;
    bool uinput_module_loaded;
    bool input_group_available;
    const char *user;
};

void input_proxy_print_wrapped_values(
    FILE *stream,
    const char *label,
    const char *const values[],
    size_t value_count
);

void input_proxy_print_access_remediation(
    FILE *stream,
    const struct input_proxy_access_remediation *access
);

enum input_proxy_result input_proxy_inspect_device(
    FILE *stream,
    FILE *error_stream,
    const char *device_path,
    const char *sysfs_input_path,
    const char *device_input_path,
    const char *uinput_path,
    const char *udev_data_path
);

#endif
