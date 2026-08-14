#include <input_proxy/virtual_device.h>

#include "virtual_device_internal.h"

#include <errno.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

static struct libevdev *test_source;
static int create_result;
static int create_calls;
static int destroy_calls;
static int write_result;
static int write_calls;
static int fail_write_call;
static int template_failures;
static int query_create_result;
static int query_create_calls;
static int key_values[KEY_MAX + 1];
static bool key_supported[KEY_MAX + 1];
static int slot_values[8];
static bool slot_supported[8];
static int slot_count;
static struct input_event written_events[32];
static struct libevdev_uinput *const test_uinput =
    (struct libevdev_uinput *)1;

const struct libevdev *input_proxy_source_device_get_libevdev(
    const struct input_proxy_source_device *device)
{
    (void)device;
    return test_source;
}

int libevdev_uinput_create_from_device(
    const struct libevdev *device,
    int uinput_fd,
    struct libevdev_uinput **uinput_device)
{
    const struct input_absinfo *absolute_info;

    create_calls++;
    if (uinput_fd != LIBEVDEV_UINPUT_OPEN_MANAGED) {
        template_failures++;
    }
    if (strcmp(libevdev_get_name(device), "proxy test device") != 0) {
        template_failures++;
    }
    if (libevdev_get_phys(device) != NULL) {
        template_failures++;
    }
    if (strcmp(libevdev_get_uniq(device), "source-unique-id") != 0) {
        template_failures++;
    }
    if (libevdev_get_id_bustype(device) != BUS_USB ||
        libevdev_get_id_vendor(device) != 0x1234 ||
        libevdev_get_id_product(device) != 0x5678 ||
        libevdev_get_id_version(device) != 9) {
        template_failures++;
    }
    if (!libevdev_has_property(device, INPUT_PROP_DIRECT) ||
        !libevdev_has_event_code(device, EV_KEY, BTN_TOUCH) ||
        !libevdev_has_event_code(device, EV_ABS, ABS_X)) {
        template_failures++;
    }
    absolute_info = libevdev_get_abs_info(device, ABS_X);
    if (absolute_info == NULL || absolute_info->minimum != 10 ||
        absolute_info->maximum != 1000 || absolute_info->resolution != 20) {
        template_failures++;
    }

    if (create_result == 0) {
        *uinput_device = test_uinput;
    }
    return create_result;
}

void libevdev_uinput_destroy(struct libevdev_uinput *device)
{
    if (device != test_uinput) {
        template_failures++;
    }
    destroy_calls++;
}

const char *libevdev_uinput_get_devnode(struct libevdev_uinput *device)
{
    return device == test_uinput ? "/dev/null" : NULL;
}

int libevdev_new_from_fd(int fd, struct libevdev **device)
{
    (void)fd;
    query_create_calls++;
    if (query_create_result < 0) {
        return query_create_result;
    }

    *device = libevdev_new();
    return *device == NULL ? -ENOMEM : 0;
}

int libevdev_fetch_event_value(
    const struct libevdev *device,
    unsigned int type,
    unsigned int code,
    int *value)
{
    (void)device;
    if (type != EV_KEY || code > KEY_MAX || !key_supported[code]) {
        return 0;
    }

    *value = key_values[code];
    return 1;
}

int libevdev_get_num_slots(const struct libevdev *device)
{
    (void)device;
    return slot_count;
}

int libevdev_fetch_slot_value(
    const struct libevdev *device,
    unsigned int slot,
    unsigned int code,
    int *value)
{
    (void)device;
    if (slot >= (unsigned int)slot_count || code != ABS_MT_TRACKING_ID ||
        !slot_supported[slot]) {
        return 0;
    }

    *value = slot_values[slot];
    return 1;
}

int libevdev_uinput_write_event(
    const struct libevdev_uinput *device,
    unsigned int type,
    unsigned int code,
    int value)
{
    if (device != test_uinput) {
        template_failures++;
    }
    if (write_calls < 32) {
        written_events[write_calls] = (struct input_event) {
            .type = type,
            .code = code,
            .value = value
        };
    }
    write_calls++;
    if (write_calls == fail_write_call) {
        return -EIO;
    }
    return write_result;
}

static void reset_neutralization_state(void)
{
    unsigned int slot;

    memset(key_values, 0, sizeof(key_values));
    memset(key_supported, 0, sizeof(key_supported));
    memset(slot_supported, 1, sizeof(slot_supported));
    memset(written_events, 0, sizeof(written_events));
    for (slot = 0; slot < 8; slot++) {
        slot_values[slot] = -1;
    }
    key_supported[KEY_A] = true;
    key_supported[KEY_B] = true;
    key_supported[BTN_TOUCH] = true;
    slot_count = 4;
    write_calls = 0;
    write_result = 0;
    fail_write_call = 0;
    query_create_result = 0;
}

static int expect_event(
    const char *test_name,
    int index,
    unsigned int type,
    unsigned int code,
    int value)
{
    if (index < write_calls && written_events[index].type == type &&
        written_events[index].code == code &&
        written_events[index].value == value) {
        return 0;
    }

    fprintf(stderr, "%s: event %d did not match\n", test_name, index);
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

static int initialize_source(void)
{
    const struct input_absinfo absolute_info = {
        .minimum = 10,
        .maximum = 1000,
        .resolution = 20
    };
    const struct input_absinfo slot_info = {
        .minimum = 0,
        .maximum = 3
    };
    const struct input_absinfo tracking_id_info = {
        .minimum = 0,
        .maximum = 65535
    };

    test_source = libevdev_new();
    if (test_source == NULL) {
        return 1;
    }

    libevdev_set_name(test_source, "source device");
    libevdev_set_phys(test_source, "source/phys");
    libevdev_set_uniq(test_source, "source-unique-id");
    libevdev_set_id_bustype(test_source, BUS_USB);
    libevdev_set_id_vendor(test_source, 0x1234);
    libevdev_set_id_product(test_source, 0x5678);
    libevdev_set_id_version(test_source, 9);

    if (libevdev_enable_property(test_source, INPUT_PROP_DIRECT) != 0 ||
        libevdev_enable_event_code(test_source, EV_KEY, KEY_A, NULL) != 0 ||
        libevdev_enable_event_code(test_source, EV_KEY, KEY_B, NULL) != 0 ||
        libevdev_enable_event_code(test_source, EV_KEY, BTN_TOUCH, NULL) != 0 ||
        libevdev_enable_event_code(
            test_source,
            EV_ABS,
            ABS_X,
            &absolute_info
        ) != 0 ||
        libevdev_enable_event_code(
            test_source,
            EV_ABS,
            ABS_MT_SLOT,
            &slot_info
        ) != 0 ||
        libevdev_enable_event_code(
            test_source,
            EV_ABS,
            ABS_MT_TRACKING_ID,
            &tracking_id_info
        ) != 0) {
        libevdev_free(test_source);
        test_source = NULL;
        return 1;
    }

    return 0;
}

int main(void)
{
    struct input_proxy_source_device *const source_device =
        (struct input_proxy_source_device *)1;
    struct input_proxy_virtual_device *device;
    struct input_event event;
    int failures = 0;

    if (initialize_source() != 0) {
        fprintf(stderr, "failed to initialize test source\n");
        return 1;
    }

    failures += expect_result(
        "null output pointer",
        input_proxy_virtual_device_create(
            NULL,
            source_device,
            "proxy test device"
        ),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );

    device = (struct input_proxy_virtual_device *)1;
    failures += expect_result(
        "null source device",
        input_proxy_virtual_device_create(
            &device,
            NULL,
            "proxy test device"
        ),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );
    if (device != NULL) {
        fprintf(stderr, "null source device: output pointer was not cleared\n");
        failures++;
    }

    device = (struct input_proxy_virtual_device *)1;
    failures += expect_result(
        "null device name",
        input_proxy_virtual_device_create(&device, source_device, NULL),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );
    if (device != NULL) {
        fprintf(stderr, "null device name: output pointer was not cleared\n");
        failures++;
    }

    create_result = -EINVAL;
    device = (struct input_proxy_virtual_device *)1;
    failures += expect_result(
        "uinput creation failure",
        input_proxy_virtual_device_create(
            &device,
            source_device,
            "proxy test device"
        ),
        INPUT_PROXY_ERROR_VIRTUAL_DEVICE_CREATE_FAILED
    );
    if (device != NULL) {
        fprintf(stderr, "uinput creation failure: output pointer was not cleared\n");
        failures++;
    }

    create_result = 0;
    failures += expect_result(
        "successful creation",
        input_proxy_virtual_device_create(
            &device,
            source_device,
            "proxy test device"
        ),
        INPUT_PROXY_SUCCESS
    );
    if (input_proxy_virtual_device_get_libevdev_uinput(device) != test_uinput) {
        fprintf(stderr, "successful creation: uinput ownership was not retained\n");
        failures++;
    }

    if (!input_proxy_virtual_device_is_compatible(device, source_device)) {
        fprintf(stderr, "unchanged source was unexpectedly incompatible\n");
        failures++;
    }
    if (input_proxy_virtual_device_is_compatible(NULL, source_device) ||
        input_proxy_virtual_device_is_compatible(device, NULL)) {
        fprintf(stderr, "null compatibility input was accepted\n");
        failures++;
    }

    libevdev_set_event_value(test_source, EV_ABS, ABS_X, 500);
    if (!input_proxy_virtual_device_is_compatible(device, source_device)) {
        fprintf(stderr, "absolute-axis state changed compatibility\n");
        failures++;
    }

    libevdev_set_id_vendor(test_source, 0xabcd);
    if (input_proxy_virtual_device_is_compatible(device, source_device)) {
        fprintf(stderr, "changed source identity was accepted\n");
        failures++;
    }
    libevdev_set_id_vendor(test_source, 0x1234);

    libevdev_set_abs_maximum(test_source, ABS_X, 1001);
    if (input_proxy_virtual_device_is_compatible(device, source_device)) {
        fprintf(stderr, "changed absolute-axis definition was accepted\n");
        failures++;
    }
    libevdev_set_abs_maximum(test_source, ABS_X, 1000);

    if (libevdev_enable_event_code(
            test_source,
            EV_KEY,
            KEY_C,
            NULL
        ) != 0) {
        fprintf(stderr, "failed to add incompatible test capability\n");
        failures++;
    } else if (input_proxy_virtual_device_is_compatible(
            device,
            source_device
        )) {
        fprintf(stderr, "changed source capabilities were accepted\n");
        failures++;
    }

    event = (struct input_event) {
        .time = { .tv_sec = 123, .tv_usec = 456 },
        .type = EV_KEY,
        .code = BTN_TOUCH,
        .value = 1
    };
    failures += expect_result(
        "successful event write",
        input_proxy_virtual_device_write_event(device, &event),
        INPUT_PROXY_SUCCESS
    );

    event = (struct input_event) {
        .type = EV_SYN,
        .code = SYN_REPORT,
        .value = 0
    };
    failures += expect_result(
        "synchronization boundary write",
        input_proxy_virtual_device_write_event(device, &event),
        INPUT_PROXY_SUCCESS
    );

    failures += expect_result(
        "null write device",
        input_proxy_virtual_device_write_event(NULL, &event),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );
    failures += expect_result(
        "null write event",
        input_proxy_virtual_device_write_event(device, NULL),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );

    write_result = -EIO;
    failures += expect_result(
        "event write failure",
        input_proxy_virtual_device_write_event(device, &event),
        INPUT_PROXY_ERROR_EVENT_WRITE_FAILED
    );
    failures += expect_event(
        "successful event write",
        0,
        EV_KEY,
        BTN_TOUCH,
        1
    );
    failures += expect_event(
        "synchronization boundary write",
        1,
        EV_SYN,
        SYN_REPORT,
        0
    );

    reset_neutralization_state();
    failures += expect_result(
        "already neutral",
        input_proxy_virtual_device_neutralize(device),
        INPUT_PROXY_SUCCESS
    );
    if (write_calls != 0) {
        fprintf(stderr, "already neutral: unnecessary events emitted\n");
        failures++;
    }

    reset_neutralization_state();
    key_supported[KEY_A] = true;
    key_supported[KEY_B] = true;
    key_supported[BTN_TOUCH] = true;
    key_values[KEY_A] = 1;
    key_values[KEY_B] = 2;
    key_values[BTN_TOUCH] = 1;
    failures += expect_result(
        "active keys",
        input_proxy_virtual_device_neutralize(device),
        INPUT_PROXY_SUCCESS
    );
    if (write_calls != 4) {
        fprintf(stderr, "active keys: unexpected event count %d\n", write_calls);
        failures++;
    }
    failures += expect_event("active keys", 0, EV_KEY, KEY_A, 0);
    failures += expect_event("active keys", 1, EV_KEY, KEY_B, 0);
    failures += expect_event("active keys", 2, EV_KEY, BTN_TOUCH, 0);
    failures += expect_event("active keys", 3, EV_SYN, SYN_REPORT, 0);

    reset_neutralization_state();
    slot_count = 4;
    slot_values[0] = 17;
    slot_values[2] = 23;
    failures += expect_result(
        "active multitouch slots",
        input_proxy_virtual_device_neutralize(device),
        INPUT_PROXY_SUCCESS
    );
    if (write_calls != 5) {
        fprintf(
            stderr,
            "active multitouch slots: unexpected event count %d\n",
            write_calls
        );
        failures++;
    }
    failures += expect_event(
        "active multitouch slots",
        0,
        EV_ABS,
        ABS_MT_SLOT,
        0
    );
    failures += expect_event(
        "active multitouch slots",
        1,
        EV_ABS,
        ABS_MT_TRACKING_ID,
        -1
    );
    failures += expect_event(
        "active multitouch slots",
        2,
        EV_ABS,
        ABS_MT_SLOT,
        2
    );
    failures += expect_event(
        "active multitouch slots",
        3,
        EV_ABS,
        ABS_MT_TRACKING_ID,
        -1
    );
    failures += expect_event(
        "active multitouch slots",
        4,
        EV_SYN,
        SYN_REPORT,
        0
    );

    reset_neutralization_state();
    key_values[KEY_A] = 1;
    key_supported[KEY_B] = false;
    failures += expect_result(
        "advertised key state unavailable",
        input_proxy_virtual_device_neutralize(device),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (write_calls != 0) {
        fprintf(stderr, "advertised key state unavailable: events emitted\n");
        failures++;
    }

    reset_neutralization_state();
    key_supported[KEY_C] = false;
    failures += expect_result(
        "unsupported key ignored",
        input_proxy_virtual_device_neutralize(device),
        INPUT_PROXY_SUCCESS
    );
    if (write_calls != 0) {
        fprintf(stderr, "unsupported key ignored: events emitted\n");
        failures++;
    }

    reset_neutralization_state();
    key_supported[BTN_TOUCH] = false;
    failures += expect_result(
        "advertised touch state unavailable",
        input_proxy_virtual_device_neutralize(device),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );

    reset_neutralization_state();
    slot_supported[2] = false;
    slot_values[0] = 17;
    failures += expect_result(
        "multitouch slot state unavailable",
        input_proxy_virtual_device_neutralize(device),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );
    if (write_calls != 0) {
        fprintf(stderr, "multitouch slot state unavailable: events emitted\n");
        failures++;
    }

    reset_neutralization_state();
    query_create_result = -EIO;
    failures += expect_result(
        "state query failure",
        input_proxy_virtual_device_neutralize(device),
        INPUT_PROXY_ERROR_EVENT_READ_FAILED
    );

    reset_neutralization_state();
    key_supported[KEY_A] = true;
    key_values[KEY_A] = 1;
    fail_write_call = 1;
    failures += expect_result(
        "neutralizing write failure",
        input_proxy_virtual_device_neutralize(device),
        INPUT_PROXY_ERROR_EVENT_WRITE_FAILED
    );

    reset_neutralization_state();
    key_values[KEY_A] = 1;
    fail_write_call = 2;
    failures += expect_result(
        "final synchronization write failure",
        input_proxy_virtual_device_neutralize(device),
        INPUT_PROXY_ERROR_EVENT_WRITE_FAILED
    );

    failures += expect_result(
        "null neutralization device",
        input_proxy_virtual_device_neutralize(NULL),
        INPUT_PROXY_ERROR_INVALID_ARGUMENT
    );

    input_proxy_virtual_device_destroy(device);
    input_proxy_virtual_device_destroy(NULL);

    if (create_calls != 2 || destroy_calls != 1 ||
        query_create_calls != 10 || template_failures != 0) {
        fprintf(
            stderr,
            "unexpected calls or contents: create=%d destroy=%d queries=%d "
            "template failures=%d\n",
            create_calls,
            destroy_calls,
            query_create_calls,
            template_failures
        );
        failures++;
    }

    libevdev_free(test_source);
    return failures == 0 ? 0 : 1;
}
