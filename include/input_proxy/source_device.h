#ifndef INPUT_PROXY_SOURCE_DEVICE_H
#define INPUT_PROXY_SOURCE_DEVICE_H

#include <input_proxy/compiler.h>
#include <input_proxy/result.h>
#include <linux/input.h>

struct input_proxy_source_device;

INPUT_PROXY_ATTRIBUTE_NODISCARD
enum input_proxy_result input_proxy_source_device_open(
    struct input_proxy_source_device **device,
    const char *source_path
);

void input_proxy_source_device_close(
    struct input_proxy_source_device *device
);

/*
 * Read one event in normal libevdev mode.
 *
 * INPUT_PROXY_SUCCESS returns an event in event. A SYN_DROPPED event returns
 * INPUT_PROXY_EVENT_SYNC_REQUIRED and is also stored in event. No available
 * event returns INPUT_PROXY_EVENT_UNAVAILABLE, and device loss returns
 * INPUT_PROXY_ERROR_SOURCE_DISCONNECTED.
 */
INPUT_PROXY_ATTRIBUTE_NODISCARD
enum input_proxy_result input_proxy_source_device_read_event(
    struct input_proxy_source_device *device,
    struct input_event *event
);

/*
 * Read one reconstructed event in libevdev synchronization mode.
 *
 * INPUT_PROXY_SUCCESS returns an event in event. INPUT_PROXY_EVENT_UNAVAILABLE
 * means synchronization recovery is complete, and device loss returns
 * INPUT_PROXY_ERROR_SOURCE_DISCONNECTED.
 */
INPUT_PROXY_ATTRIBUTE_NODISCARD
enum input_proxy_result input_proxy_source_device_read_sync_event(
    struct input_proxy_source_device *device,
    struct input_event *event
);

#endif
