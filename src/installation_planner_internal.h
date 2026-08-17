#ifndef INPUT_PROXY_INSTALLATION_PLANNER_INTERNAL_H
#define INPUT_PROXY_INSTALLATION_PLANNER_INTERNAL_H

#include <input_proxy/proxy_session.h>

#include "installed_instance_internal.h"
#include "deployment_readiness_internal.h"

#include <stdbool.h>
#include <stdint.h>

struct input_proxy_installation_request {
    const char *source_path;
    const char *instance_name;
    uint64_t activity_timeout_ms;
    uint64_t detection_throttle_ms;
    bool running_motion_activity;
    bool paused_motion_activity;
    bool start_paused;
    bool source_supplied;
    bool instance_name_supplied;
    bool activity_timeout_supplied;
    bool detection_throttle_supplied;
    bool running_motion_supplied;
    bool paused_motion_supplied;
    bool start_paused_supplied;
};

enum input_proxy_installation_plan_result {
    INPUT_PROXY_INSTALLATION_PLAN_SUCCESS = 0,
    INPUT_PROXY_INSTALLATION_PLAN_INVALID_REQUEST,
    INPUT_PROXY_INSTALLATION_PLAN_INVALID_INSTANCE_NAME,
    INPUT_PROXY_INSTALLATION_PLAN_INSTALLED_NAME_COLLISION,
    INPUT_PROXY_INSTALLATION_PLAN_RUNTIME_NAME_COLLISION,
    INPUT_PROXY_INSTALLATION_PLAN_STORE_FAILED,
    INPUT_PROXY_INSTALLATION_PLAN_OWNERSHIP_FAILED,
    INPUT_PROXY_INSTALLATION_PLAN_OUT_OF_MEMORY
};

struct input_proxy_installation_plan;

void input_proxy_installation_request_init(
    struct input_proxy_installation_request *request
);

enum input_proxy_installation_plan_result input_proxy_installation_plan_create(
    struct input_proxy_installation_plan **plan,
    const struct input_proxy_installation_request *request,
    const struct input_proxy_installed_instance_store *store
);

const struct input_proxy_session_config *input_proxy_installation_plan_config(
    const struct input_proxy_installation_plan *plan
);

enum input_proxy_installation_plan_result input_proxy_installation_plan_assess(
    struct input_proxy_installation_plan *plan,
    const struct input_proxy_deployment_environment *environment
);

const struct input_proxy_deployment_readiness *
input_proxy_installation_plan_readiness(
    const struct input_proxy_installation_plan *plan
);

void input_proxy_installation_plan_destroy(
    struct input_proxy_installation_plan *plan
);

#endif
