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

int main(void)
{
    char service_name[128];
    int failures = 0;

    if (input_proxy_runtime_control_derive_service_name(
            service_name, sizeof(service_name), "Touchscreen_1") != 0 ||
        strcmp(service_name,
            "net.controlforge.InputProxy1.Instance.Touchscreen_1") != 0) {
        fprintf(stderr, "service-name derivation failed\n");
        failures++;
    }
    if (input_proxy_runtime_control_derive_service_name(
            service_name, sizeof(service_name), "invalid.name") != -EINVAL) {
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
