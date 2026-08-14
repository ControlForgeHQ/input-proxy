#define _POSIX_C_SOURCE 200809L

#include <input_proxy/source_device.h>

#include "source_device_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct input_proxy_source_device {
    int file_descriptor;
    struct libevdev *evdev;
};

enum input_proxy_result input_proxy_source_device_open(
    struct input_proxy_source_device **device,
    const char *source_path)
{
    struct input_proxy_source_device *new_device;
    int file_descriptor;
    int libevdev_result;

    if (device == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    *device = NULL;

    if (source_path == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    file_descriptor = open(source_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (file_descriptor < 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE;
        }
        if (errno == EACCES) {
            return INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED;
        }

        return INPUT_PROXY_ERROR_SOURCE_OPEN_FAILED;
    }

    new_device = calloc(1, sizeof(*new_device));
    if (new_device == NULL) {
        close(file_descriptor);
        return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
    }

    new_device->file_descriptor = file_descriptor;

    libevdev_result = libevdev_new_from_fd(
        new_device->file_descriptor,
        &new_device->evdev
    );
    if (libevdev_result < 0) {
        input_proxy_source_device_close(new_device);
        return INPUT_PROXY_ERROR_SOURCE_OPEN_FAILED;
    }

    *device = new_device;

    return INPUT_PROXY_SUCCESS;
}

void input_proxy_source_device_close(
    struct input_proxy_source_device *device)
{
    if (device == NULL) {
        return;
    }

    libevdev_free(device->evdev);
    close(device->file_descriptor);
    free(device);
}

enum input_proxy_result input_proxy_source_device_read_event(
    struct input_proxy_source_device *device,
    struct input_event *event)
{
    int libevdev_result;

    if (device == NULL || event == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    libevdev_result = libevdev_next_event(
        device->evdev,
        LIBEVDEV_READ_FLAG_NORMAL,
        event
    );

    if (libevdev_result == LIBEVDEV_READ_STATUS_SUCCESS) {
        return INPUT_PROXY_SUCCESS;
    }
    if (libevdev_result == LIBEVDEV_READ_STATUS_SYNC) {
        return INPUT_PROXY_EVENT_SYNC_REQUIRED;
    }
    if (libevdev_result == -EAGAIN) {
        return INPUT_PROXY_EVENT_UNAVAILABLE;
    }
    if (libevdev_result == -ENODEV || libevdev_result == -ENXIO) {
        return INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    }

    return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
}

enum input_proxy_result input_proxy_source_device_read_sync_event(
    struct input_proxy_source_device *device,
    struct input_event *event)
{
    int libevdev_result;

    if (device == NULL || event == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    libevdev_result = libevdev_next_event(
        device->evdev,
        LIBEVDEV_READ_FLAG_SYNC,
        event
    );

    if (libevdev_result == LIBEVDEV_READ_STATUS_SUCCESS ||
        libevdev_result == LIBEVDEV_READ_STATUS_SYNC) {
        return INPUT_PROXY_SUCCESS;
    }
    if (libevdev_result == -EAGAIN) {
        return INPUT_PROXY_EVENT_UNAVAILABLE;
    }
    if (libevdev_result == -ENODEV || libevdev_result == -ENXIO) {
        return INPUT_PROXY_ERROR_SOURCE_DISCONNECTED;
    }

    return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
}

const struct libevdev *input_proxy_source_device_get_libevdev(
    const struct input_proxy_source_device *device)
{
    if (device == NULL) {
        return NULL;
    }

    return device->evdev;
}

static enum input_proxy_result append_state_event(
    struct input_proxy_source_state *state,
    unsigned int type,
    unsigned int code,
    int value)
{
    struct input_event *events;

    events = realloc(
        state->events,
        (state->event_count + 1) * sizeof(*state->events)
    );
    if (events == NULL) {
        return INPUT_PROXY_ERROR_OUT_OF_MEMORY;
    }

    state->events = events;
    state->events[state->event_count] = (struct input_event) {
        .type = type,
        .code = code,
        .value = value
    };
    state->event_count++;
    return INPUT_PROXY_SUCCESS;
}

static enum input_proxy_result capture_event_type(
    const struct libevdev *evdev,
    struct input_proxy_source_state *state,
    unsigned int type,
    unsigned int maximum_code)
{
    unsigned int code;

    for (code = 0; code <= maximum_code; code++) {
        enum input_proxy_result result;
        int value;

        if (!libevdev_has_event_code(evdev, type, code)) {
            continue;
        }
        if (!libevdev_fetch_event_value(evdev, type, code, &value)) {
            return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
        }

        result = append_state_event(state, type, code, value);
        if (result != INPUT_PROXY_SUCCESS) {
            return result;
        }
    }

    return INPUT_PROXY_SUCCESS;
}

static bool is_multitouch_axis(unsigned int code)
{
    return code >= ABS_MT_TOUCH_MAJOR && code <= ABS_MT_TOOL_Y;
}

static enum input_proxy_result capture_absolute_state(
    const struct libevdev *evdev,
    struct input_proxy_source_state *state,
    bool type_b_multitouch)
{
    unsigned int code;

    for (code = 0; code <= ABS_MAX; code++) {
        enum input_proxy_result result;
        int value;

        if (!libevdev_has_event_code(evdev, EV_ABS, code)) {
            continue;
        }
        if (type_b_multitouch &&
            (code == ABS_MT_SLOT || is_multitouch_axis(code))) {
            continue;
        }
        if (!libevdev_fetch_event_value(evdev, EV_ABS, code, &value)) {
            return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
        }

        result = append_state_event(state, EV_ABS, code, value);
        if (result != INPUT_PROXY_SUCCESS) {
            return result;
        }
    }

    return INPUT_PROXY_SUCCESS;
}

static enum input_proxy_result capture_multitouch_state(
    const struct libevdev *evdev,
    struct input_proxy_source_state *state)
{
    enum input_proxy_result result;
    int current_slot;
    int slot_count;
    int slot;

    if (!libevdev_fetch_event_value(
            evdev,
            EV_ABS,
            ABS_MT_SLOT,
            &current_slot
        )) {
        return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    }

    slot_count = libevdev_get_num_slots(evdev);
    if (slot_count <= 0) {
        return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
    }

    for (slot = 0; slot < slot_count; slot++) {
        unsigned int code;
        int value;

        result = append_state_event(state, EV_ABS, ABS_MT_SLOT, slot);
        if (result != INPUT_PROXY_SUCCESS) {
            return result;
        }

        if (!libevdev_fetch_slot_value(
                evdev,
                (unsigned int)slot,
                ABS_MT_TRACKING_ID,
                &value
            )) {
            return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
        }
        result = append_state_event(
            state,
            EV_ABS,
            ABS_MT_TRACKING_ID,
            value
        );
        if (result != INPUT_PROXY_SUCCESS) {
            return result;
        }

        if (value < 0) {
            continue;
        }

        for (code = ABS_MT_TOUCH_MAJOR; code <= ABS_MT_TOOL_Y; code++) {
            if (code == ABS_MT_TRACKING_ID ||
                !libevdev_has_event_code(evdev, EV_ABS, code)) {
                continue;
            }
            if (!libevdev_fetch_slot_value(
                    evdev,
                    (unsigned int)slot,
                    code,
                    &value
                )) {
                return INPUT_PROXY_ERROR_EVENT_READ_FAILED;
            }
            result = append_state_event(state, EV_ABS, code, value);
            if (result != INPUT_PROXY_SUCCESS) {
                return result;
            }
        }
    }

    return append_state_event(state, EV_ABS, ABS_MT_SLOT, current_slot);
}

enum input_proxy_result input_proxy_source_device_capture_state(
    const struct input_proxy_source_device *device,
    struct input_proxy_source_state *state)
{
    bool type_b_multitouch;
    enum input_proxy_result result;

    if (device == NULL || state == NULL) {
        return INPUT_PROXY_ERROR_INVALID_ARGUMENT;
    }

    memset(state, 0, sizeof(*state));

    result = capture_event_type(device->evdev, state, EV_KEY, KEY_MAX);
    if (result != INPUT_PROXY_SUCCESS) {
        goto error;
    }
    result = capture_event_type(device->evdev, state, EV_SW, SW_MAX);
    if (result != INPUT_PROXY_SUCCESS) {
        goto error;
    }

    type_b_multitouch = libevdev_has_event_code(
        device->evdev,
        EV_ABS,
        ABS_MT_SLOT
    ) && libevdev_has_event_code(
        device->evdev,
        EV_ABS,
        ABS_MT_TRACKING_ID
    );

    result = capture_absolute_state(device->evdev, state, type_b_multitouch);
    if (result != INPUT_PROXY_SUCCESS) {
        goto error;
    }
    if (type_b_multitouch) {
        result = capture_multitouch_state(device->evdev, state);
        if (result != INPUT_PROXY_SUCCESS) {
            goto error;
        }
    }

    return INPUT_PROXY_SUCCESS;

error:
    input_proxy_source_state_destroy(state);
    return result;
}

void input_proxy_source_state_destroy(struct input_proxy_source_state *state)
{
    if (state == NULL) {
        return;
    }

    free(state->events);
    memset(state, 0, sizeof(*state));
}
