#include "libinput_status_internal.h"

#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysmacros.h>

enum input_proxy_libinput_status input_proxy_read_libinput_status(
    const char *udev_data_path,
    const struct stat *device_status,
    input_proxy_udev_property_handler property_handler,
    void *property_handler_userdata)
{
    char path[PATH_MAX];
    char line[1024];
    FILE *file;
    bool ignored = false;

    if (udev_data_path == NULL || device_status == NULL ||
        !S_ISCHR(device_status->st_mode) ||
        snprintf(path, sizeof(path), "%s/c%u:%u", udev_data_path,
            major(device_status->st_rdev), minor(device_status->st_rdev)) >=
            (int)sizeof(path)) {
        return INPUT_PROXY_LIBINPUT_STATUS_INDETERMINATE;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return INPUT_PROXY_LIBINPUT_STATUS_INDETERMINATE;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *value;
        size_t length = strlen(line);

        while (length > 0 &&
            (line[length - 1] == '\n' || line[length - 1] == '\r')) {
            line[--length] = '\0';
        }
        if (strncmp(line, "E:", 2) != 0) {
            continue;
        }
        value = strchr(line + 2, '=');
        if (value == NULL) {
            continue;
        }
        *value++ = '\0';
        if (strcmp(line + 2, "LIBINPUT_IGNORE_DEVICE") == 0 &&
            strcmp(value, "1") == 0) {
            ignored = true;
        }
        if (property_handler != NULL) {
            property_handler(line + 2, value, property_handler_userdata);
        }
    }

    {
        const bool read_failed = ferror(file) != 0;
        const bool close_failed = fclose(file) != 0;
        if (read_failed || close_failed) {
            return INPUT_PROXY_LIBINPUT_STATUS_INDETERMINATE;
        }
    }
    return ignored ? INPUT_PROXY_LIBINPUT_STATUS_IGNORED
                   : INPUT_PROXY_LIBINPUT_STATUS_NOT_IGNORED;
}
