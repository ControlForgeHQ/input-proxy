#ifndef INPUT_PROXY_LIBINPUT_STATUS_INTERNAL_H
#define INPUT_PROXY_LIBINPUT_STATUS_INTERNAL_H

#include <sys/stat.h>

enum input_proxy_libinput_status {
    INPUT_PROXY_LIBINPUT_STATUS_INDETERMINATE = 0,
    INPUT_PROXY_LIBINPUT_STATUS_NOT_IGNORED,
    INPUT_PROXY_LIBINPUT_STATUS_IGNORED
};

typedef void (*input_proxy_udev_property_handler)(
    const char *name,
    const char *value,
    void *userdata
);

enum input_proxy_libinput_status input_proxy_read_libinput_status(
    const char *udev_data_path,
    const struct stat *device_status,
    input_proxy_udev_property_handler property_handler,
    void *property_handler_userdata
);

#endif
