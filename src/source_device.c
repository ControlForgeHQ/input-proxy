#include <input_proxy/source_device.h>

enum input_proxy_result input_proxy_source_device_open(
    struct input_proxy_source_device **device,
    const char *source_path)
{
    (void)device;
    (void)source_path;

    return INPUT_PROXY_ERROR_NOT_IMPLEMENTED;
}

void input_proxy_source_device_close(
    struct input_proxy_source_device *device)
{
    (void)device;
}