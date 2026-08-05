#include <input_proxy/virtual_device.h>

#include "source_device_internal.h"
#include "virtual_device_internal.h"

#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>

struct input_proxy_virtual_device {
    struct libevdev_uinput *uinput;
};

static enum input_proxy_result copy_capabilities(
    struct libevdev *destination,
    const struct libevdev *source)
{
    unsigned int property;
    unsigned int type;

    for (property = 0; property <= INPUT_PROP_MAX; property++) {
        if (libevdev_has_property(source, property) &&
            libevdev_enable_property(destination, property) != 0) {
            return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
        }
    }

    for (type = 0; type <= EV_MAX; type++) {
        int maximum_code;
        unsigned int code;

        if (!libevdev_has_event_type(source, type)) {
            continue;
        }

        if (libevdev_enable_event_type(destination, type) != 0) {
            return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
        }

        maximum_code = libevdev_event_type_get_max(type);
        if (maximum_code < 0) {
            continue;
        }

        for (code = 0; code <= (unsigned int)maximum_code; code++) {
            const void *data = NULL;
            int repeat_value;

            if (!libevdev_has_event_code(source, type, code)) {
                continue;
            }

            if (type == EV_ABS) {
                data = libevdev_get_abs_info(source, code);
            } else if (type == EV_REP) {
                repeat_value = libevdev_get_event_value(source, type, code);
                data = &repeat_value;
            }

            if (libevdev_enable_event_code(
                    destination,
                    type,
                    code,
                    data
                ) != 0) {
                return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
            }
        }
    }

    return INPUT_PROXY_SUCCESS;
}

static enum input_proxy_result create_template(
    struct libevdev **template,
    const struct libevdev *source,
    const char *device_name,
    const char *physical_identifier)
{
    struct libevdev *new_template;
    const char *source_value;
    enum input_proxy_result result;

    new_template = libevdev_new();
    if (new_template == NULL) {
        return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
    }

    libevdev_set_name(new_template, device_name);
    source_value = physical_identifier != NULL
        ? physical_identifier
        : libevdev_get_phys(source);
    libevdev_set_phys(new_template, source_value);
    libevdev_set_uniq(new_template, libevdev_get_uniq(source));
    libevdev_set_id_bustype(new_template, libevdev_get_id_bustype(source));
    libevdev_set_id_vendor(new_template, libevdev_get_id_vendor(source));
    libevdev_set_id_product(new_template, libevdev_get_id_product(source));
    libevdev_set_id_version(new_template, libevdev_get_id_version(source));

    result = copy_capabilities(new_template, source);
    if (result != INPUT_PROXY_SUCCESS) {
        libevdev_free(new_template);
        return result;
    }

    *template = new_template;
    return INPUT_PROXY_SUCCESS;
}

enum input_proxy_result input_proxy_virtual_device_create(
    struct input_proxy_virtual_device **device,
    const struct input_proxy_source_device *source_device,
    const char *device_name,
    const char *physical_identifier)
{
    const struct libevdev *source;
    struct input_proxy_virtual_device *new_device;
    struct libevdev *template = NULL;
    enum input_proxy_result result;
    int libevdev_result;

    if (device == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    *device = NULL;

    if (source_device == NULL || device_name == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    source = input_proxy_source_device_get_libevdev(source_device);
    if (source == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    new_device = calloc(1, sizeof(*new_device));
    if (new_device == NULL) {
        return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
    }

    result = create_template(
        &template,
        source,
        device_name,
        physical_identifier
    );
    if (result != INPUT_PROXY_SUCCESS) {
        free(new_device);
        return result;
    }

    libevdev_result = libevdev_uinput_create_from_device(
        template,
        LIBEVDEV_UINPUT_OPEN_MANAGED,
        &new_device->uinput
    );
    libevdev_free(template);

    if (libevdev_result < 0) {
        free(new_device);
        if (libevdev_result == -ENOMEM) {
            return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
        }
        if (libevdev_result == -ENOENT ||
            libevdev_result == -EACCES ||
            libevdev_result == -EPERM) {
            return INPUT_PROXY_ERROR_UINPUT_UNAVAILABLE;
        }
        return INPUT_PROXY_ERROR_VIRTUAL_DEVICE_CREATE_FAILED;
    }

    *device = new_device;
    return INPUT_PROXY_SUCCESS;
}

void input_proxy_virtual_device_destroy(
    struct input_proxy_virtual_device *device)
{
    if (device == NULL) {
        return;
    }

    if (device->uinput != NULL) {
        libevdev_uinput_destroy(device->uinput);
    }
    free(device);
}

struct libevdev_uinput *input_proxy_virtual_device_get_libevdev_uinput(
    struct input_proxy_virtual_device *device)
{
    if (device == NULL) {
        return NULL;
    }

    return device->uinput;
}
