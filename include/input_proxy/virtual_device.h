#ifndef INPUT_PROXY_VIRTUAL_DEVICE_H
#define INPUT_PROXY_VIRTUAL_DEVICE_H

#include <input_proxy/compiler.h>
#include <input_proxy/result.h>
#include <linux/input.h>

struct input_proxy_source_device;
struct input_proxy_virtual_device;

INPUT_PROXY_ATTRIBUTE_NODISCARD
enum input_proxy_result input_proxy_virtual_device_create(
    struct input_proxy_virtual_device **device,
    const struct input_proxy_source_device *source_device,
    const char *device_name
);

void input_proxy_virtual_device_destroy(
    struct input_proxy_virtual_device *device
);

/*
 * Inject one event through libevdev uinput, preserving its type, code, and
 * value. Linux assigns the emitted virtual event's timestamp.
 */
INPUT_PROXY_ATTRIBUTE_NODISCARD
enum input_proxy_result input_proxy_virtual_device_write_event(
    struct input_proxy_virtual_device *device,
    const struct input_event *event
);

#endif
