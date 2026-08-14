#ifndef INPUT_PROXY_SOURCE_DEVICE_INTERNAL_H
#define INPUT_PROXY_SOURCE_DEVICE_INTERNAL_H

#include <linux/input.h>
#include <stddef.h>

struct input_proxy_source_device;
struct libevdev;

const struct libevdev *input_proxy_source_device_get_libevdev(
    const struct input_proxy_source_device *device
);

struct input_proxy_source_state {
    struct input_event *events;
    size_t event_count;
};

enum input_proxy_result input_proxy_source_device_capture_state(
    const struct input_proxy_source_device *device,
    struct input_proxy_source_state *state
);

void input_proxy_source_state_destroy(struct input_proxy_source_state *state);

#endif
