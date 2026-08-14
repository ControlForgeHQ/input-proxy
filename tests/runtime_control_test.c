#include "runtime_control_internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

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

int main(void)
{
    char service_name[128];
    int failures = 0;

    failures += test_property_changes();

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
