#include <input_proxy/source_device.h>

#include <stdio.h>

static int expect_result(
    const char *test_name,
    enum input_proxy_result actual,
    enum input_proxy_result expected)
{
    if (actual == expected) {
        return 0;
    }

    fprintf(
        stderr,
        "%s: expected %s, got %s\n",
        test_name,
        input_proxy_result_string(expected),
        input_proxy_result_string(actual)
    );
    return 1;
}

int main(void)
{
    struct input_proxy_source_device *device;
    int failures = 0;

    failures += expect_result(
        "null output pointer",
        input_proxy_source_device_open(NULL, "/dev/null"),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );

    device = (struct input_proxy_source_device *)1;
    failures += expect_result(
        "null source path",
        input_proxy_source_device_open(&device, NULL),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );
    if (device != NULL) {
        fprintf(stderr, "null source path: output pointer was not cleared\n");
        failures++;
    }

    device = (struct input_proxy_source_device *)1;
    failures += expect_result(
        "missing source",
        input_proxy_source_device_open(
            &device,
            "/input-proxy-test-path-that-does-not-exist"
        ),
        INPUT_PROXY_ERROR_SOURCE_UNAVAILABLE
    );
    if (device != NULL) {
        fprintf(stderr, "missing source: output pointer was not cleared\n");
        failures++;
    }

    device = (struct input_proxy_source_device *)1;
    failures += expect_result(
        "non-evdev source",
        input_proxy_source_device_open(&device, "/dev/null"),
        INPUT_PROXY_ERROR_SOURCE_OPEN_FAILED
    );
    if (device != NULL) {
        fprintf(stderr, "non-evdev source: output pointer was not cleared\n");
        failures++;
    }

    input_proxy_source_device_close(NULL);

    return failures == 0 ? 0 : 1;
}
