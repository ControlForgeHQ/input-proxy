#define _POSIX_C_SOURCE 200809L

#include <input_proxy/source_device.h>

#include "source_device_internal.h"

#include <errno.h>
#include <libevdev/libevdev.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int new_from_fd_result = -ENOTTY;
static int next_event_result;
static int next_event_calls;
static unsigned int expected_next_event_flags = LIBEVDEV_READ_FLAG_NORMAL;
static int next_event_flag_failures;
static struct input_event next_event;
static bool supported_codes[EV_MAX + 1][KEY_MAX + 1];
static int current_values[EV_MAX + 1][KEY_MAX + 1];
static bool unavailable_codes[EV_MAX + 1][KEY_MAX + 1];
static int current_slot_values[4][ABS_MAX + 1];
static bool unavailable_slot_values[4][ABS_MAX + 1];
static int current_slot_count;
static int availability_ioctl_result;
static int availability_ioctl_errno;
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

int libevdev_has_event_code(
    const struct libevdev *device,
    unsigned int type,
    unsigned int code)
{
    (void)device;
    return type <= EV_MAX && code <= KEY_MAX && supported_codes[type][code];
}

int libevdev_fetch_event_value(
    const struct libevdev *device,
    unsigned int type,
    unsigned int code,
    int *value)
{
    (void)device;
    if (type > EV_MAX || code > KEY_MAX || !supported_codes[type][code] ||
        unavailable_codes[type][code]) {
        return 0;
    }
    *value = current_values[type][code];
    return 1;
}

int libevdev_get_num_slots(const struct libevdev *device)
{
    (void)device;
    return current_slot_count;
}

int libevdev_fetch_slot_value(
    const struct libevdev *device,
    unsigned int slot,
    unsigned int code,
    int *value)
{
    (void)device;
    if (slot >= (unsigned int)current_slot_count || code > ABS_MAX ||
        unavailable_slot_values[slot][code]) {
        return 0;
    }
    *value = current_slot_values[slot][code];
    return 1;
}

int ioctl(int file_descriptor, unsigned long request, ...)
{
    va_list arguments;
    int *version;

    (void)file_descriptor;
    va_start(arguments, request);
    version = va_arg(arguments, int *);
    va_end(arguments);

    if (request != EVIOCGVERSION) {
        errno = EINVAL;
        return -1;
    }
    if (availability_ioctl_result == 0) {
        *version = 1;
    } else {
        errno = availability_ioctl_errno;
    }
    return availability_ioctl_result;
}

static int expect_state_event(
    const char *test_name,
    const struct input_proxy_source_state *state,
    size_t index,
    unsigned int type,
    unsigned int code,
    int value)
{
    if (index < state->event_count && state->events[index].type == type &&
        state->events[index].code == code &&
        state->events[index].value == value) {
        return 0;
    }

    fprintf(stderr, "%s: state event %zu did not match\n", test_name, index);
    return 1;
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
    char inaccessible_path[] = "/tmp/input-proxy-source-test-XXXXXX";
    int inaccessible_fd;
    struct input_proxy_source_state state = {0};

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

    inaccessible_fd = mkstemp(inaccessible_path);
    if (inaccessible_fd < 0 || close(inaccessible_fd) != 0 ||
        chmod(inaccessible_path, 0000) != 0) {
        fprintf(stderr, "permission denied source: setup failed\n");
        if (inaccessible_fd >= 0) {
            close(inaccessible_fd);
        }
        unlink(inaccessible_path);
        return 1;
    }
    device = (struct input_proxy_source_device *)1;
    failures += expect_result(
        "permission denied source",
        input_proxy_source_device_open(&device, inaccessible_path),
        INPUT_PROXY_ERROR_SOURCE_PERMISSION_DENIED
    );
    if (device != NULL) {
        fprintf(stderr, "permission denied source: output pointer was not cleared\n");
        failures++;
    }
    chmod(inaccessible_path, 0600);
    unlink(inaccessible_path);

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

    memset(supported_codes, 0, sizeof(supported_codes));
    memset(current_values, 0, sizeof(current_values));
    memset(unavailable_codes, 0, sizeof(unavailable_codes));
    memset(current_slot_values, 0, sizeof(current_slot_values));
    memset(unavailable_slot_values, 0, sizeof(unavailable_slot_values));
    supported_codes[EV_KEY][KEY_A] = true;
    supported_codes[EV_KEY][KEY_B] = true;
    supported_codes[EV_KEY][BTN_TOUCH] = true;
    supported_codes[EV_SW][SW_LID] = true;
    supported_codes[EV_ABS][ABS_X] = true;
    supported_codes[EV_ABS][ABS_MT_SLOT] = true;
    supported_codes[EV_ABS][ABS_MT_TRACKING_ID] = true;
    supported_codes[EV_ABS][ABS_MT_POSITION_X] = true;
    current_values[EV_KEY][KEY_A] = 1;
    current_values[EV_KEY][BTN_TOUCH] = 1;
    current_values[EV_SW][SW_LID] = 1;
    current_values[EV_ABS][ABS_X] = 317;
    current_values[EV_ABS][ABS_MT_SLOT] = 0;
    current_slot_count = 2;
    current_slot_values[0][ABS_MT_TRACKING_ID] = 42;
    current_slot_values[0][ABS_MT_POSITION_X] = 700;
    current_slot_values[1][ABS_MT_TRACKING_ID] = -1;
    current_slot_values[1][ABS_MT_POSITION_X] = 900;

    failures += expect_result(
        "complete current state",
        input_proxy_source_device_capture_state(device, &state),
        INPUT_PROXY_SUCCESS
    );
    if (state.event_count != 11) {
        fprintf(stderr, "complete current state: unexpected event count\n");
        failures++;
    }
    failures += expect_state_event(
        "active key", &state, 0, EV_KEY, KEY_A, 1
    );
    failures += expect_state_event(
        "inactive key", &state, 1, EV_KEY, KEY_B, 0
    );
    failures += expect_state_event(
        "BTN_TOUCH", &state, 2, EV_KEY, BTN_TOUCH, 1
    );
    failures += expect_state_event(
        "switch", &state, 3, EV_SW, SW_LID, 1
    );
    failures += expect_state_event(
        "absolute axis", &state, 4, EV_ABS, ABS_X, 317
    );
    failures += expect_state_event(
        "first slot", &state, 5, EV_ABS, ABS_MT_SLOT, 0
    );
    failures += expect_state_event(
        "first tracking id", &state, 6, EV_ABS, ABS_MT_TRACKING_ID, 42
    );
    failures += expect_state_event(
        "first contact value", &state, 7, EV_ABS, ABS_MT_POSITION_X, 700
    );
    failures += expect_state_event(
        "second tracking id", &state, 9, EV_ABS, ABS_MT_TRACKING_ID, -1
    );
    failures += expect_state_event(
        "restore current slot", &state, 10, EV_ABS, ABS_MT_SLOT, 0
    );
    input_proxy_source_state_destroy(&state);

    current_slot_values[1][ABS_MT_TRACKING_ID] = 77;
    failures += expect_result(
        "multiple active contacts",
        input_proxy_source_device_capture_state(device, &state),
        INPUT_PROXY_SUCCESS
    );
    if (state.event_count != 12) {
        fprintf(stderr, "multiple active contacts: unexpected event count\n");
        failures++;
    }
    failures += expect_state_event(
        "second active tracking id",
        &state,
        9,
        EV_ABS,
        ABS_MT_TRACKING_ID,
        77
    );
    failures += expect_state_event(
        "second active contact value",
        &state,
        10,
        EV_ABS,
        ABS_MT_POSITION_X,
        900
    );
    input_proxy_source_state_destroy(&state);

    current_slot_values[0][ABS_MT_TRACKING_ID] = -1;
    current_slot_values[1][ABS_MT_TRACKING_ID] = -1;
    failures += expect_result(
        "no active contacts",
        input_proxy_source_device_capture_state(device, &state),
        INPUT_PROXY_SUCCESS
    );
    if (state.event_count != 10) {
        fprintf(stderr, "no active contacts: unexpected event count\n");
        failures++;
    }
    input_proxy_source_state_destroy(&state);
    current_slot_values[0][ABS_MT_TRACKING_ID] = 42;

    unavailable_codes[EV_KEY][KEY_A] = true;
    failures += expect_result(
        "advertised key state unavailable",
        input_proxy_source_device_capture_state(device, &state),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (state.events != NULL || state.event_count != 0) {
        fprintf(stderr, "failed state capture was not cleaned up\n");
        failures++;
    }
    unavailable_codes[EV_KEY][KEY_A] = false;

    unavailable_codes[EV_SW][SW_LID] = true;
    failures += expect_result(
        "advertised switch state unavailable",
        input_proxy_source_device_capture_state(device, &state),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    unavailable_codes[EV_SW][SW_LID] = false;

    unavailable_codes[EV_ABS][ABS_X] = true;
    failures += expect_result(
        "advertised absolute state unavailable",
        input_proxy_source_device_capture_state(device, &state),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    unavailable_codes[EV_ABS][ABS_X] = false;

    unavailable_slot_values[1][ABS_MT_TRACKING_ID] = true;
    failures += expect_result(
        "multitouch state unavailable",
        input_proxy_source_device_capture_state(device, &state),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    unavailable_slot_values[1][ABS_MT_TRACKING_ID] = false;

    availability_ioctl_result = 0;
    failures += expect_result(
        "source remains available",
        input_proxy_source_device_check_available(device),
        INPUT_PROXY_SUCCESS
    );
    availability_ioctl_result = -1;
    availability_ioctl_errno = ENODEV;
    failures += expect_result(
        "source disconnected during synchronization",
        input_proxy_source_device_check_available(device),
        INPUT_PROXY_ERROR_SOURCE_DISCONNECTED
    );
    availability_ioctl_errno = EIO;
    failures += expect_result(
        "source availability check failed",
        input_proxy_source_device_check_available(device),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    failures += expect_result(
        "null source availability check",
        input_proxy_source_device_check_available(NULL),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
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
