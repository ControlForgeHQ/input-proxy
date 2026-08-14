#define _POSIX_C_SOURCE 200809L

#include <input_proxy/virtual_device.h>

#include "source_device_internal.h"
#include "virtual_device_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <libevdev/libevdev-uinput.h>
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct input_proxy_virtual_device {
    struct libevdev_uinput *uinput;
    struct libevdev *capabilities;
};

static bool nullable_strings_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return left == right;
    }

    return strcmp(left, right) == 0;
}

static bool absolute_info_equal(
    const struct input_absinfo *left,
    const struct input_absinfo *right)
{
    if (left == NULL || right == NULL) {
        return left == right;
    }

    return left->minimum == right->minimum &&
        left->maximum == right->maximum &&
        left->fuzz == right->fuzz &&
        left->flat == right->flat &&
        left->resolution == right->resolution;
}

static bool capabilities_equal(
    const struct libevdev *represented,
    const struct libevdev *source)
{
    unsigned int property;
    unsigned int type;

    if (!nullable_strings_equal(
            libevdev_get_uniq(represented),
            libevdev_get_uniq(source)
        ) ||
        libevdev_get_id_bustype(represented) !=
            libevdev_get_id_bustype(source) ||
        libevdev_get_id_vendor(represented) !=
            libevdev_get_id_vendor(source) ||
        libevdev_get_id_product(represented) !=
            libevdev_get_id_product(source) ||
        libevdev_get_id_version(represented) !=
            libevdev_get_id_version(source)) {
        return false;
    }

    for (property = 0; property <= INPUT_PROP_MAX; property++) {
        if (libevdev_has_property(represented, property) !=
            libevdev_has_property(source, property)) {
            return false;
        }
    }

    for (type = 0; type <= EV_MAX; type++) {
        int maximum_code;
        unsigned int code;

        if (libevdev_has_event_type(represented, type) !=
            libevdev_has_event_type(source, type)) {
            return false;
        }

        maximum_code = libevdev_event_type_get_max(type);
        if (maximum_code < 0) {
            continue;
        }

        for (code = 0; code <= (unsigned int)maximum_code; code++) {
            if (libevdev_has_event_code(represented, type, code) !=
                libevdev_has_event_code(source, type, code)) {
                return false;
            }
            if (!libevdev_has_event_code(source, type, code)) {
                continue;
            }

            if (type == EV_ABS && !absolute_info_equal(
                    libevdev_get_abs_info(represented, code),
                    libevdev_get_abs_info(source, code)
                )) {
                return false;
            }
            if (type == EV_REP &&
                libevdev_get_event_value(represented, type, code) !=
                    libevdev_get_event_value(source, type, code)) {
                return false;
            }
        }
    }

    return true;
}

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
    const char *device_name)
{
    struct libevdev *new_template;
    enum input_proxy_result result;

    new_template = libevdev_new();
    if (new_template == NULL) {
        return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
    }

    libevdev_set_name(new_template, device_name);
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
    const char *device_name)
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
        device_name
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

    if (libevdev_result < 0) {
        libevdev_free(template);
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

    new_device->capabilities = template;
    *device = new_device;
    return INPUT_PROXY_SUCCESS;
}

bool input_proxy_virtual_device_is_compatible(
    const struct input_proxy_virtual_device *device,
    const struct input_proxy_source_device *source_device)
{
    const struct libevdev *source;

    if (device == NULL || source_device == NULL) {
        return false;
    }

    source = input_proxy_source_device_get_libevdev(source_device);
    if (source == NULL) {
        return false;
    }

    return capabilities_equal(device->capabilities, source);
}

static enum input_proxy_result write_neutralizing_event(
    struct input_proxy_virtual_device *device,
    unsigned int type,
    unsigned int code,
    int value)
{
    const struct input_event event = {
        .type = type,
        .code = code,
        .value = value
    };

    return input_proxy_virtual_device_write_event(device, &event);
}

enum input_proxy_result input_proxy_virtual_device_neutralize(
    struct input_proxy_virtual_device *device)
{
    const char *device_node;
    struct libevdev *state = NULL;
    int *slot_tracking_ids = NULL;
    enum input_proxy_result result = INPUT_PROXY_SUCCESS;
    int key_values[KEY_MAX + 1] = {0};
    bool changed = false;
    bool has_type_b_multitouch;
    int file_descriptor;
    unsigned int code;
    int slot_count;
    int slot;

    if (device == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    device_node = libevdev_uinput_get_devnode(device->uinput);
    if (device_node == NULL) {
        return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    }

    file_descriptor = open(device_node, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (file_descriptor < 0) {
        return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    }

    if (libevdev_new_from_fd(file_descriptor, &state) < 0) {
        close(file_descriptor);
        return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    }

    for (code = 0; code <= KEY_MAX; code++) {
        if (!libevdev_has_event_code(
                device->capabilities,
                EV_KEY,
                code
            )) {
            continue;
        }

        if (!libevdev_fetch_event_value(
                state,
                EV_KEY,
                code,
                &key_values[code]
            )) {
            result = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
            goto cleanup;
        }
    }

    has_type_b_multitouch = libevdev_has_event_code(
        device->capabilities,
        EV_ABS,
        ABS_MT_SLOT
    ) && libevdev_has_event_code(
        device->capabilities,
        EV_ABS,
        ABS_MT_TRACKING_ID
    );

    slot_count = 0;
    if (has_type_b_multitouch) {
        slot_count = libevdev_get_num_slots(state);
        if (slot_count <= 0) {
            result = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
            goto cleanup;
        }

        slot_tracking_ids = calloc(
            (size_t)slot_count,
            sizeof(*slot_tracking_ids)
        );
        if (slot_tracking_ids == NULL) {
            result = INPUT_PROXY_ERROR_OUT_OF_MEMORY;
            goto cleanup;
        }

        for (slot = 0; slot < slot_count; slot++) {
            if (!libevdev_fetch_slot_value(
                    state,
                    (unsigned int)slot,
                    ABS_MT_TRACKING_ID,
                    &slot_tracking_ids[slot]
                )) {
                result = INPUT_PROXY_ERROR_EVENT_READ_FAILED;
                goto cleanup;
            }
        }
    }

    for (code = 0; code <= KEY_MAX; code++) {
        if (!libevdev_has_event_code(
                device->capabilities,
                EV_KEY,
                code
            ) || key_values[code] == 0) {
            continue;
        }

        result = write_neutralizing_event(device, EV_KEY, code, 0);
        if (result != INPUT_PROXY_SUCCESS) {
            goto cleanup;
        }
        changed = true;
    }

    for (slot = 0; slot < slot_count; slot++) {
        if (slot_tracking_ids[slot] < 0) {
            continue;
        }

        result = write_neutralizing_event(
            device,
            EV_ABS,
            ABS_MT_SLOT,
            slot
        );
        if (result != INPUT_PROXY_SUCCESS) {
            goto cleanup;
        }

        result = write_neutralizing_event(
            device,
            EV_ABS,
            ABS_MT_TRACKING_ID,
            -1
        );
        if (result != INPUT_PROXY_SUCCESS) {
            goto cleanup;
        }
        changed = true;
    }

    if (changed) {
        result = write_neutralizing_event(device, EV_SYN, SYN_REPORT, 0);
    }

cleanup:
    free(slot_tracking_ids);
    libevdev_free(state);
    close(file_descriptor);
    return result;
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
    libevdev_free(device->capabilities);
    free(device);
}

enum input_proxy_result input_proxy_virtual_device_write_event(
    struct input_proxy_virtual_device *device,
    const struct input_event *event)
{
    int libevdev_result;

    if (device == NULL || event == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    libevdev_result = libevdev_uinput_write_event(
        device->uinput,
        event->type,
        event->code,
        event->value
    );
    if (libevdev_result < 0) {
        return INPUT_PROXY_ERROR_EVENT_WRITE_FAILED;
    }

    return INPUT_PROXY_SUCCESS;
}

struct libevdev_uinput *input_proxy_virtual_device_get_libevdev_uinput(
    struct input_proxy_virtual_device *device)
{
    if (device == NULL) {
        return NULL;
    }

    return device->uinput;
}
