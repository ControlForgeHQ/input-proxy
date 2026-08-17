#define _POSIX_C_SOURCE 200809L

#include "installation_planner_internal.h"
#include "instance_name_internal.h"
#include "runtime_policy_internal.h"

#include <input_proxy/proxy_session.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;

static void expect(bool condition, const char *description)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static struct input_proxy_installation_request make_request(const char *name)
{
    struct input_proxy_installation_request request;

    input_proxy_installation_request_init(&request);
    request.source_path = "/dev/input/by-id/example";
    request.instance_name = name;
    request.source_supplied = true;
    request.instance_name_supplied = true;
    return request;
}

int main(void)
{
    char directory[] = "/tmp/input-proxy-plan-test-XXXXXX";
    struct input_proxy_installed_instance_store *store = NULL;
    struct input_proxy_installation_plan *plan = NULL;
    struct input_proxy_instance_name *ownership = NULL;
    struct input_proxy_installation_request request;
    const struct input_proxy_session_config *config;
    enum input_proxy_installation_plan_result result;
    uint64_t duration;
    bool enabled;

    expect(mkdtemp(directory) != NULL, "create isolated artifact directory");
    expect(input_proxy_installed_instance_store_create_for_directory(
        &store,
        directory
    ) == INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS, "create isolated store");

    request = make_request("PlanDefaults");
    result = input_proxy_installation_plan_create(&plan, &request, store);
    expect(result == INPUT_PROXY_INSTALLATION_PLAN_SUCCESS,
        "defaulted request plans successfully");
    config = input_proxy_installation_plan_config(plan);
    expect(config != NULL &&
        strcmp(config->source_path, request.source_path) == 0 &&
        strcmp(config->instance_name, request.instance_name) == 0,
        "plan owns resolved source and name");
    expect(config != NULL &&
        config->activity_timeout_ms == INPUT_PROXY_DEFAULT_ACTIVITY_TIMEOUT_MS &&
        config->detection_throttle_ms ==
            INPUT_PROXY_DEFAULT_DETECTION_THROTTLE_MS &&
        config->running_motion_activity && config->paused_motion_activity &&
        !config->start_paused && !config->verbose,
        "plan contains the complete persistent default snapshot");
    expect(input_proxy_instance_name_acquire(
        &ownership,
        "PlanDefaults"
    ) == INPUT_PROXY_ERROR_INSTANCE_NAME_OWNED,
        "successful plan retains runtime name reservation");
    input_proxy_installation_plan_destroy(plan);
    plan = NULL;
    expect(input_proxy_instance_name_acquire(
        &ownership,
        "PlanDefaults"
    ) == INPUT_PROXY_SUCCESS, "destroying plan releases reservation");
    input_proxy_instance_name_release(ownership);
    ownership = NULL;

    request = make_request("ExplicitPolicy");
    request.activity_timeout_ms = 7;
    request.detection_throttle_ms = 9;
    request.running_motion_activity = false;
    request.paused_motion_activity = false;
    request.start_paused = true;
    request.activity_timeout_supplied = true;
    request.detection_throttle_supplied = true;
    request.running_motion_supplied = true;
    request.paused_motion_supplied = true;
    request.start_paused_supplied = true;
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_SUCCESS, "explicit policy resolves");
    config = input_proxy_installation_plan_config(plan);
    expect(config->activity_timeout_ms == 7 &&
        config->detection_throttle_ms == 9 &&
        !config->running_motion_activity &&
        !config->paused_motion_activity && config->start_paused,
        "explicit policy is preserved");
    input_proxy_installation_plan_destroy(plan);
    plan = NULL;

    expect(input_proxy_runtime_policy_parse_duration("0", &duration) &&
        duration == 0, "duration accepts zero");
    expect(input_proxy_runtime_policy_parse_duration("4294967295", &duration) &&
        duration == UINT32_MAX, "duration accepts maximum");
    expect(!input_proxy_runtime_policy_parse_duration("4294967296", &duration) &&
        !input_proxy_runtime_policy_parse_duration("-1", &duration) &&
        !input_proxy_runtime_policy_parse_duration("1x", &duration),
        "duration rejects out-of-range and non-digit values");
    expect(input_proxy_runtime_policy_parse_on_off("on", &enabled) && enabled &&
        input_proxy_runtime_policy_parse_on_off("off", &enabled) && !enabled &&
        !input_proxy_runtime_policy_parse_on_off("true", &enabled),
        "on-off grammar is authoritative");

    request = make_request("Invalid Name");
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_INVALID_INSTANCE_NAME,
        "planner shares Instance Name validation");

    request = make_request("InstalledCollision");
    {
        struct input_proxy_session_config artifact_config = {
            .source_path = request.source_path,
            .instance_name = request.instance_name,
            .activity_timeout_ms = request.activity_timeout_ms,
            .detection_throttle_ms = request.detection_throttle_ms,
            .running_motion_activity = request.running_motion_activity,
            .paused_motion_activity = request.paused_motion_activity
        };
        expect(input_proxy_installed_instance_create(store, &artifact_config) ==
            INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS, "create occupied artifact");
    }
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_INSTALLED_NAME_COLLISION,
        "occupied artifact reports installed-name collision");
    expect(input_proxy_instance_name_acquire(
        &ownership,
        "InstalledCollision"
    ) == INPUT_PROXY_SUCCESS, "installed collision does not reserve runtime name");
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_INSTALLED_NAME_COLLISION,
        "installed collision takes precedence when runtime name is also owned");
    input_proxy_instance_name_release(ownership);
    ownership = NULL;

    request = make_request("RuntimeCollision");
    expect(input_proxy_instance_name_acquire(
        &ownership,
        request.instance_name
    ) == INPUT_PROXY_SUCCESS, "hold runtime name directly");
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_RUNTIME_NAME_COLLISION,
        "owned socket reports runtime-name collision");
    input_proxy_instance_name_release(ownership);
    ownership = NULL;
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_SUCCESS,
        "runtime collision failure leaves no reservation");
    input_proxy_installation_plan_destroy(plan);

    expect(input_proxy_installed_instance_remove(
        store,
        "InstalledCollision"
    ) == INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS, "remove test artifact");
    input_proxy_installed_instance_store_destroy(store);
    expect(rmdir(directory) == 0, "remove isolated artifact directory");

    if (failures != 0) {
        fprintf(stderr, "%d installation planner test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
