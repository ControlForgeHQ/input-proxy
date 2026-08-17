#define _POSIX_C_SOURCE 200809L

#include "installation_planner_internal.h"

#include "instance_name_internal.h"
#include "runtime_policy_internal.h"

#include <stdlib.h>
#include <string.h>

struct input_proxy_installation_plan {
    struct input_proxy_session_config config;
    char *source_path;
    char *instance_name;
    struct input_proxy_instance_name *name_ownership;
};

void input_proxy_installation_request_init(
    struct input_proxy_installation_request *request)
{
    struct input_proxy_session_config defaults;

    if (request == NULL) {
        return;
    }
    input_proxy_runtime_policy_defaults(&defaults);
    *request = (struct input_proxy_installation_request) {
        .activity_timeout_ms = defaults.activity_timeout_ms,
        .detection_throttle_ms = defaults.detection_throttle_ms,
        .running_motion_activity = defaults.running_motion_activity,
        .paused_motion_activity = defaults.paused_motion_activity,
        .start_paused = defaults.start_paused
    };
}

static enum input_proxy_installation_plan_result map_name_result(
    enum input_proxy_result result)
{
    if (result == INPUT_PROXY_ERROR_INVALID_INSTANCE_NAME) {
        return INPUT_PROXY_INSTALLATION_PLAN_INVALID_INSTANCE_NAME;
    }
    if (result == INPUT_PROXY_ERROR_INSTANCE_NAME_OWNED) {
        return INPUT_PROXY_INSTALLATION_PLAN_RUNTIME_NAME_COLLISION;
    }
    if (result == INPUT_PROXY_ERROR_OUT_OF_MEMORY) {
        return INPUT_PROXY_INSTALLATION_PLAN_OUT_OF_MEMORY;
    }
    return INPUT_PROXY_INSTALLATION_PLAN_OWNERSHIP_FAILED;
}

enum input_proxy_installation_plan_result input_proxy_installation_plan_create(
    struct input_proxy_installation_plan **plan,
    const struct input_proxy_installation_request *request,
    const struct input_proxy_installed_instance_store *store)
{
    struct input_proxy_installation_plan *new_plan;
    struct input_proxy_session_config config;
    enum input_proxy_installed_instance_result store_result;
    enum input_proxy_result name_result;
    bool exists;

    if (plan == NULL) {
        return INPUT_PROXY_INSTALLATION_PLAN_INVALID_REQUEST;
    }
    *plan = NULL;
    if (request == NULL || store == NULL || !request->source_supplied ||
        !request->instance_name_supplied || request->source_path == NULL ||
        request->source_path[0] == '\0' ||
        strchr(request->source_path, '\n') != NULL ||
        request->instance_name == NULL) {
        return INPUT_PROXY_INSTALLATION_PLAN_INVALID_REQUEST;
    }

    config = (struct input_proxy_session_config) {
        .source_path = request->source_path,
        .instance_name = request->instance_name,
        .activity_timeout_ms = request->activity_timeout_ms,
        .detection_throttle_ms = request->detection_throttle_ms,
        .running_motion_activity = request->running_motion_activity,
        .paused_motion_activity = request->paused_motion_activity,
        .start_paused = request->start_paused
    };
    if (!input_proxy_runtime_policy_is_valid(&config)) {
        return INPUT_PROXY_INSTALLATION_PLAN_INVALID_REQUEST;
    }
    name_result = input_proxy_instance_name_validate(request->instance_name);
    if (name_result != INPUT_PROXY_SUCCESS) {
        return map_name_result(name_result);
    }

    store_result = input_proxy_installed_instance_exists(
        store,
        request->instance_name,
        &exists
    );
    if (store_result != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
        return store_result == INPUT_PROXY_INSTALLED_INSTANCE_OUT_OF_MEMORY
            ? INPUT_PROXY_INSTALLATION_PLAN_OUT_OF_MEMORY
            : INPUT_PROXY_INSTALLATION_PLAN_STORE_FAILED;
    }
    if (exists) {
        return INPUT_PROXY_INSTALLATION_PLAN_INSTALLED_NAME_COLLISION;
    }

    new_plan = calloc(1, sizeof(*new_plan));
    if (new_plan == NULL) {
        return INPUT_PROXY_INSTALLATION_PLAN_OUT_OF_MEMORY;
    }
    name_result = input_proxy_instance_name_acquire(
        &new_plan->name_ownership,
        request->instance_name
    );
    if (name_result != INPUT_PROXY_SUCCESS) {
        free(new_plan);
        return map_name_result(name_result);
    }
    new_plan->source_path = strdup(request->source_path);
    new_plan->instance_name = strdup(request->instance_name);
    if (new_plan->source_path == NULL || new_plan->instance_name == NULL) {
        input_proxy_installation_plan_destroy(new_plan);
        return INPUT_PROXY_INSTALLATION_PLAN_OUT_OF_MEMORY;
    }
    new_plan->config = config;
    new_plan->config.source_path = new_plan->source_path;
    new_plan->config.instance_name = new_plan->instance_name;
    *plan = new_plan;
    return INPUT_PROXY_INSTALLATION_PLAN_SUCCESS;
}

const struct input_proxy_session_config *input_proxy_installation_plan_config(
    const struct input_proxy_installation_plan *plan)
{
    return plan == NULL ? NULL : &plan->config;
}

void input_proxy_installation_plan_destroy(
    struct input_proxy_installation_plan *plan)
{
    if (plan == NULL) {
        return;
    }
    input_proxy_instance_name_release(plan->name_ownership);
    free(plan->source_path);
    free(plan->instance_name);
    free(plan);
}
