#define _POSIX_C_SOURCE 200809L

#include <input_proxy/source_device.h>

#include "source_device_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
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

const struct libevdev *input_proxy_source_device_get_libevdev(
    const struct input_proxy_source_device *device)
{
    if (device == NULL) {
        return NULL;
    }

    return device->evdev;
}
