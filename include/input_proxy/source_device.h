#ifndef INPUT_PROXY_SOURCE_DEVICE_H
#define INPUT_PROXY_SOURCE_DEVICE_H

#include <input_proxy/compiler.h>
#include <input_proxy/result.h>

struct input_proxy_source_device;

INPUT_PROXY_ATTRIBUTE_NODISCARD
enum input_proxy_result input_proxy_source_device_open(
    struct input_proxy_source_device **device,
    const char *source_path
);

void input_proxy_source_device_close(
    struct input_proxy_source_device *device
);

#endif