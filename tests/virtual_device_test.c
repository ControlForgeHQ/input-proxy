#include <input_proxy/virtual_device.h>

#include "virtual_device_internal.h"

#include <errno.h>
#include <libevdev/libevdev.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

static struct libevdev *test_source;
static int create_result;
static int create_calls;
static int destroy_calls;
static int write_result;
static int write_calls;
static int write_failures;
static int template_failures;
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

int libevdev_uinput_write_event(
    const struct libevdev_uinput *device,
    unsigned int type,
    unsigned int code,
    int value)
{
    write_calls++;
    if (device != test_uinput) {
        write_failures++;
    }
    if (write_calls == 1 &&
        (type != EV_KEY || code != BTN_TOUCH || value != 1)) {
        write_failures++;
    }
    if (write_calls == 2 &&
        (type != EV_SYN || code != SYN_REPORT || value != 0)) {
        write_failures++;
    }
    return write_result;
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
        libevdev_enable_event_code(test_source, EV_KEY, BTN_TOUCH, NULL) != 0 ||
        libevdev_enable_event_code(
            test_source,
            EV_ABS,
            ABS_X,
            &absolute_info
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

    input_proxy_virtual_device_destroy(device);
    input_proxy_virtual_device_destroy(NULL);

    if (create_calls != 2 || destroy_calls != 1 || write_calls != 3 ||
        write_failures != 0 || template_failures != 0) {
        fprintf(
            stderr,
            "unexpected calls or contents: create=%d destroy=%d write=%d "
            "write failures=%d template failures=%d\n",
            create_calls,
            destroy_calls,
            write_calls,
            write_failures,
            template_failures
        );
        failures++;
    }

    libevdev_free(test_source);
    return failures == 0 ? 0 : 1;
}
