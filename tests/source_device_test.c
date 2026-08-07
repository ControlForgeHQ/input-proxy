#include <input_proxy/source_device.h>

#include <errno.h>
#include <libevdev/libevdev.h>
#include <stdio.h>
#include <string.h>

static int new_from_fd_result = -ENOTTY;
static int next_event_result;
static int next_event_calls;
static unsigned int expected_next_event_flags = LIBEVDEV_READ_FLAG_NORMAL;
static int next_event_flag_failures;
static struct input_event next_event;
static struct libevdev *const test_evdev = (struct libevdev *)1;

int libevdev_new_from_fd(int file_descriptor, struct libevdev **device)
{
    (void)file_descriptor;

    if (new_from_fd_result == 0) {
        *device = test_evdev;
    }
    return new_from_fd_result;
}

void libevdev_free(struct libevdev *device)
{
    (void)device;
}

int libevdev_next_event(
    struct libevdev *device,
    unsigned int flags,
    struct input_event *event)
{
    next_event_calls++;
    if (device != test_evdev || flags != expected_next_event_flags) {
        next_event_flag_failures++;
    }
    if (next_event_result == LIBEVDEV_READ_STATUS_SUCCESS ||
        next_event_result == LIBEVDEV_READ_STATUS_SYNC) {
        *event = next_event;
    }
    return next_event_result;
}

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
    struct input_event event;
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

    new_from_fd_result = 0;
    failures += expect_result(
        "successful open",
        input_proxy_source_device_open(&device, "/dev/null"),
        INPUT_PROXY_SUCCESS
    );

    failures += expect_result(
        "null read device",
        input_proxy_source_device_read_event(NULL, &event),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );
    failures += expect_result(
        "null read event",
        input_proxy_source_device_read_event(device, NULL),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );

    next_event = (struct input_event) {
        .time = { .tv_sec = 123, .tv_usec = 456 },
        .type = EV_KEY,
        .code = KEY_A,
        .value = 1
    };
    next_event_result = LIBEVDEV_READ_STATUS_SUCCESS;
    memset(&event, 0, sizeof(event));
    failures += expect_result(
        "successful event read",
        input_proxy_source_device_read_event(device, &event),
        INPUT_PROXY_SUCCESS
    );
    if (memcmp(&event, &next_event, sizeof(event)) != 0) {
        fprintf(stderr, "successful event read: event was not preserved\n");
        failures++;
    }

    next_event = (struct input_event) {
        .type = EV_SYN,
        .code = SYN_DROPPED,
        .value = 0
    };
    next_event_result = LIBEVDEV_READ_STATUS_SYNC;
    failures += expect_result(
        "synchronization required",
        input_proxy_source_device_read_event(device, &event),
        INPUT_PROXY_EVENT_SYNC_REQUIRED
    );
    if (memcmp(&event, &next_event, sizeof(event)) != 0) {
        fprintf(stderr, "synchronization required: event was not returned\n");
        failures++;
    }

    next_event_result = -EAGAIN;
    failures += expect_result(
        "event temporarily unavailable",
        input_proxy_source_device_read_event(device, &event),
        INPUT_PROXY_EVENT_UNAVAILABLE
    );

    next_event_result = -ENODEV;
    failures += expect_result(
        "source disconnected",
        input_proxy_source_device_read_event(device, &event),
        INPUT_PROXY_ERROR_SOURCE_DISCONNECTED
    );

    next_event_result = -ENXIO;
    failures += expect_result(
        "source lost",
        input_proxy_source_device_read_event(device, &event),
        INPUT_PROXY_ERROR_SOURCE_DISCONNECTED
    );

    next_event_result = -EIO;
    failures += expect_result(
        "unrecoverable read error",
        input_proxy_source_device_read_event(device, &event),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );

    failures += expect_result(
        "null sync read device",
        input_proxy_source_device_read_sync_event(NULL, &event),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );
    failures += expect_result(
        "null sync read event",
        input_proxy_source_device_read_sync_event(device, NULL),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );

    expected_next_event_flags = LIBEVDEV_READ_FLAG_SYNC;
    next_event = (struct input_event) {
        .type = EV_KEY,
        .code = KEY_B,
        .value = 1
    };
    next_event_result = LIBEVDEV_READ_STATUS_SYNC;
    failures += expect_result(
        "successful synchronization event read",
        input_proxy_source_device_read_sync_event(device, &event),
        INPUT_PROXY_SUCCESS
    );
    if (memcmp(&event, &next_event, sizeof(event)) != 0) {
        fprintf(stderr, "successful sync read: event was not preserved\n");
        failures++;
    }

    next_event_result = -EAGAIN;
    failures += expect_result(
        "synchronization complete",
        input_proxy_source_device_read_sync_event(device, &event),
        INPUT_PROXY_EVENT_UNAVAILABLE
    );

    next_event_result = -ENODEV;
    failures += expect_result(
        "sync source disconnected",
        input_proxy_source_device_read_sync_event(device, &event),
        INPUT_PROXY_ERROR_SOURCE_DISCONNECTED
    );

    next_event_result = -EIO;
    failures += expect_result(
        "unrecoverable sync read error",
        input_proxy_source_device_read_sync_event(device, &event),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );

    if (next_event_calls != 10 || next_event_flag_failures != 0) {
        fprintf(
            stderr,
            "unexpected event reads: calls=%d flag failures=%d\n",
            next_event_calls,
            next_event_flag_failures
        );
        failures++;
    }

    input_proxy_source_device_close(device);

    input_proxy_source_device_close(NULL);

    return failures == 0 ? 0 : 1;
}
