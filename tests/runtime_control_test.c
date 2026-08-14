#include "runtime_control_internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <systemd/sd-bus.h>

static struct input_proxy_runtime_control **dispatch_control;
static struct input_proxy_runtime_control_state *dispatch_state;
static bool fail_property_notification;
static bool change_property_during_dispatch;
static bool dispatch_active;
static bool callback_continued_after_notification;
static bool teardown_during_dispatch;
static int slot_unref_calls;
static int bus_unref_calls;

int __wrap_sd_bus_open_system(sd_bus **bus)
{
    *bus = (sd_bus *)1;
    return 0;
}

int __wrap_sd_bus_add_object_vtable(sd_bus *bus, sd_bus_slot **slot,
    const char *path, const char *interface, const sd_bus_vtable *vtable,
    void *userdata)
{
    (void)bus; (void)path; (void)interface; (void)vtable; (void)userdata;
    *slot = (sd_bus_slot *)1;
    return 0;
}

int __wrap_sd_bus_request_name(sd_bus *bus, const char *name, uint64_t flags)
{
    (void)bus; (void)name; (void)flags;
    return 1;
}

int __wrap_sd_bus_emit_properties_changed_strv(sd_bus *bus,
    const char *path, const char *interface, char **names)
{
    (void)bus; (void)path; (void)interface; (void)names;
    return fail_property_notification ? -EIO : 0;
}

sd_bus_slot *__wrap_sd_bus_slot_unref(sd_bus_slot *slot)
{
    if (slot != NULL) {
        slot_unref_calls++;
        teardown_during_dispatch |= dispatch_active;
    }
    return NULL;
}

sd_bus *__wrap_sd_bus_flush_close_unref(sd_bus *bus)
{
    if (bus != NULL) {
        bus_unref_calls++;
        teardown_during_dispatch |= dispatch_active;
    }
    return NULL;
}

int __wrap_sd_bus_process(sd_bus *bus, sd_bus_message **message)
{
    (void)bus; (void)message;
    dispatch_active = true;
    if (change_property_during_dispatch) {
        const struct input_proxy_runtime_control_changes changes = {
            .properties = INPUT_PROXY_RUNTIME_CONTROL_PAUSED,
            .paused = true
        };

        change_property_during_dispatch = false;
        input_proxy_runtime_control_apply_changes(
            dispatch_control, dispatch_state, &changes);
        if (*dispatch_control == NULL) {
            teardown_during_dispatch = true;
        }
        callback_continued_after_notification = *dispatch_control != NULL;
    }
    dispatch_active = false;
    return 0;
}

static enum input_proxy_result ignore_pause_request(void *userdata, bool paused)
{
    (void)userdata; (void)paused;
    return INPUT_PROXY_SUCCESS;
}

static int expect_failure(const char *name,
    enum input_proxy_runtime_control_failure actual,
    enum input_proxy_runtime_control_failure expected)
{
    if (actual == expected) {
        return 0;
    }
    fprintf(stderr, "%s: unexpected failure classification\n", name);
    return 1;
}

static int test_property_changes(void)
{
    struct input_proxy_runtime_control_state state = {0};
    struct input_proxy_runtime_control_changes changes = {
        .properties = INPUT_PROXY_RUNTIME_CONTROL_SOURCE_AVAILABLE,
        .source_available = true
    };
    size_t changed_count;
    int failures = 0;

    changed_count = input_proxy_runtime_control_apply_changes(
        NULL, &state, &changes);
    if (changed_count != 1 || !state.source_available) {
        fprintf(stderr, "false-to-true property transition failed\n");
        failures++;
    }

    changed_count = input_proxy_runtime_control_apply_changes(
        NULL, &state, &changes);
    if (changed_count != 0 || !state.source_available) {
        fprintf(stderr, "redundant property transition was not ignored\n");
        failures++;
    }

    changes.source_available = false;
    changed_count = input_proxy_runtime_control_apply_changes(
        NULL, &state, &changes);
    if (changed_count != 1 || state.source_available) {
        fprintf(stderr, "true-to-false property transition failed\n");
        failures++;
    }

    changes.source_available = true;
    changed_count = input_proxy_runtime_control_apply_changes(
        NULL, &state, &changes);
    if (changed_count != 1 || !state.source_available) {
        fprintf(stderr, "reconnect property transition failed\n");
        failures++;
    }

    changes = (struct input_proxy_runtime_control_changes) {
        .properties = INPUT_PROXY_RUNTIME_CONTROL_PAUSED |
            INPUT_PROXY_RUNTIME_CONTROL_ACTIVITY_WHILE_RUNNING,
        .paused = true,
        .activity_while_running = true
    };
    changed_count = input_proxy_runtime_control_apply_changes(
        NULL, &state, &changes);
    if (changed_count != 2 || !state.paused ||
        !state.activity_while_running) {
        fprintf(stderr, "multi-property transition failed\n");
        failures++;
    }

    return failures;
}

static int test_property_failure_cleanup(void)
{
    struct input_proxy_runtime_control_state state = {
        .instance_name = "TestInstance",
        .source_path = "/dev/input/test"
    };
    const struct input_proxy_runtime_control_changes changes = {
        .properties = INPUT_PROXY_RUNTIME_CONTROL_PAUSED,
        .paused = true
    };
    struct input_proxy_runtime_control *control;
    int failures = 0;

    fail_property_notification = true;
    slot_unref_calls = 0;
    bus_unref_calls = 0;
    teardown_during_dispatch = false;
    control = input_proxy_runtime_control_create(
        &state, ignore_pause_request, NULL);
    input_proxy_runtime_control_apply_changes(&control, &state, &changes);
    if (control != NULL || !state.paused || slot_unref_calls != 1 ||
        bus_unref_calls != 1 || teardown_during_dispatch) {
        fprintf(stderr, "safe immediate property-failure cleanup failed\n");
        failures++;
    }

    state.paused = false;
    slot_unref_calls = 0;
    bus_unref_calls = 0;
    teardown_during_dispatch = false;
    callback_continued_after_notification = false;
    control = input_proxy_runtime_control_create(
        &state, ignore_pause_request, NULL);
    dispatch_control = &control;
    dispatch_state = &state;
    change_property_during_dispatch = true;
    input_proxy_runtime_control_process(&control);
    if (control != NULL || !state.paused || slot_unref_calls != 1 ||
        bus_unref_calls != 1 || teardown_during_dispatch ||
        !callback_continued_after_notification) {
        fprintf(stderr, "deferred callback property-failure cleanup failed\n");
        failures++;
    }

    input_proxy_runtime_control_process(&control);
    fail_property_notification = false;
    return failures;
}

int main(void)
{
    char service_name[128];
    int failures = 0;

    failures += test_property_changes();
    failures += test_property_failure_cleanup();

    if (input_proxy_runtime_control_derive_service_name(
            service_name, sizeof(service_name), "Touchscreen_1") != 0 ||
        strcmp(service_name,
            "net.controlforge.InputProxy1.Instance.Touchscreen_1") != 0) {
        fprintf(stderr, "service-name derivation failed\n");
        failures++;
    }
    if (input_proxy_runtime_control_derive_service_name(
            service_name, sizeof(service_name), "invalid/name") != -EINVAL) {
        fprintf(stderr, "invalid derived identifier was accepted\n");
        failures++;
    }
    if (input_proxy_runtime_control_derive_service_name(
            service_name, 8, "Touchscreen_1") != -ENOBUFS) {
        fprintf(stderr, "truncated service name was accepted\n");
        failures++;
    }
    failures += expect_failure("unavailable connection",
        input_proxy_runtime_control_classify_connection_failure(ENOENT),
        INPUT_PROXY_RUNTIME_CONTROL_SYSTEM_BUS_UNAVAILABLE);
    failures += expect_failure("rejected connection",
        input_proxy_runtime_control_classify_connection_failure(EACCES),
        INPUT_PROXY_RUNTIME_CONTROL_CONNECTION_REJECTED);
    failures += expect_failure("owned name",
        input_proxy_runtime_control_classify_name_failure(EEXIST),
        INPUT_PROXY_RUNTIME_CONTROL_NAME_OWNED);
    failures += expect_failure("denied name",
        input_proxy_runtime_control_classify_name_failure(EPERM),
        INPUT_PROXY_RUNTIME_CONTROL_NAME_DENIED);
    failures += expect_failure("generic initialization",
        input_proxy_runtime_control_classify_name_failure(ENOMEM),
        INPUT_PROXY_RUNTIME_CONTROL_INITIALIZATION_FAILED);

    return failures == 0 ? 0 : 1;
}
