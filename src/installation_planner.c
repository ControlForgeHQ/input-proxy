#define _POSIX_C_SOURCE 200809L

#include "installation_planner_internal.h"

#include "instance_name_internal.h"
#include "runtime_policy_internal.h"
#include "device_discovery_internal.h"
#include "device_inspection_internal.h"
#include "libinput_status_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct input_proxy_installation_plan {
    struct input_proxy_session_config config;
    char *source_path;
    char *instance_name;
    struct input_proxy_instance_name *name_ownership;
    struct input_proxy_deployment_readiness readiness;
    struct input_proxy_deployment_resolution resolution;
    char preferred_source_path[PATH_MAX];
    bool readiness_available;
    bool resolution_available;
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

static bool group_matches(const struct input_proxy_deployment_environment *env,
                          gid_t group)
{
    size_t index;

    if (env->service_gid == group) return true;
    for (index = 0; index < env->service_group_count; ++index) {
        if (env->service_groups[index] == group) return true;
    }
    return false;
}

static bool identity_has_access(const struct stat *status,
                                const struct input_proxy_deployment_environment *env,
                                mode_t owner_bit, mode_t group_bit,
                                mode_t other_bit)
{
    if (env->service_uid == 0) return true;
    if (env->service_uid == status->st_uid)
        return (status->st_mode & owner_bit) != 0;
    if (group_matches(env, status->st_gid))
        return (status->st_mode & group_bit) != 0;
    return (status->st_mode & other_bit) != 0;
}

static int deployment_stat(const struct input_proxy_deployment_environment *env,
                           const char *path, struct stat *status)
{
    return env->stat_path == NULL ? stat(path, status)
                                  : env->stat_path(path, status,
                                                   env->stat_userdata);
}

enum input_proxy_installation_plan_result input_proxy_installation_plan_assess(
    struct input_proxy_installation_plan *plan,
    const struct input_proxy_deployment_environment *env)
{
    struct stat source_status;
    struct stat uinput_status;
    char event_node[PATH_MAX];
    char event_sysfs_path[PATH_MAX];
    struct input_proxy_device_identity identity;
    struct input_proxy_device_rule_identity rule_identity = {0};
    bool narrow_match;

    if (plan == NULL || env == NULL || env->sysfs_input_path == NULL ||
        env->device_input_path == NULL || env->uinput_path == NULL ||
        env->udev_data_path == NULL ||
        (env->service_group_count > 0 && env->service_groups == NULL))
        return INPUT_PROXY_INSTALLATION_PLAN_INVALID_REQUEST;

    memset(&plan->readiness, 0, sizeof(plan->readiness));
    memset(&plan->resolution, 0, sizeof(plan->resolution));
    plan->resolution_available = false;
    plan->config.source_path = plan->source_path;
    plan->preferred_source_path[0] = '\0';
    plan->readiness.supplied_source_path = plan->source_path;
    plan->readiness.selected_source_path = plan->source_path;
    plan->readiness.libinput_status = INPUT_PROXY_LIBINPUT_STATUS_INDETERMINATE;

    if (deployment_stat(env, plan->source_path, &source_status) != 0 ||
        !S_ISCHR(source_status.st_mode) ||
        !input_proxy_resolve_event_node(env->sysfs_input_path,
            env->device_input_path, &source_status, event_node,
            sizeof(event_node), event_sysfs_path, sizeof(event_sysfs_path)) ||
        !input_proxy_read_device_identity(event_sysfs_path, &identity)) {
        plan->readiness.blockers |= INPUT_PROXY_DEPLOYMENT_BLOCKER_SOURCE;
        plan->readiness_available = true;
        return INPUT_PROXY_INSTALLATION_PLAN_SUCCESS;
    }

    plan->readiness.physical_source = !identity.virtual_device;
    if (!plan->readiness.physical_source)
        plan->readiness.blockers |= INPUT_PROXY_DEPLOYMENT_BLOCKER_SOURCE;
    if (input_proxy_find_persistent_input_path(env->device_input_path,
            &source_status, plan->preferred_source_path,
            sizeof(plan->preferred_source_path))) {
        plan->readiness.preferred_source_path = plan->preferred_source_path;
        plan->readiness.preferred_source_differs =
            strcmp(plan->source_path, plan->preferred_source_path) != 0;
    }

    plan->readiness.libinput_status = input_proxy_read_libinput_status(
        env->udev_data_path, &source_status, input_proxy_collect_rule_identity,
        &rule_identity);
    input_proxy_rule_identity_add_kernel_identity(&rule_identity, &identity);
    plan->readiness.rule_identity = rule_identity;
    narrow_match = input_proxy_rule_identity_is_narrow(&rule_identity);
    plan->readiness.source_accessible = identity_has_access(&source_status, env,
        S_IRUSR, S_IRGRP, S_IROTH);
    if (!plan->readiness.source_accessible)
        plan->readiness.blockers |=
            INPUT_PROXY_DEPLOYMENT_BLOCKER_PACKAGE_INTEGRATION;
    plan->readiness.libinput_ignore_rule_available = narrow_match &&
        plan->readiness.libinput_status ==
            INPUT_PROXY_LIBINPUT_STATUS_NOT_IGNORED;

    plan->readiness.uinput_accessible =
        deployment_stat(env, env->uinput_path, &uinput_status) == 0 &&
        S_ISCHR(uinput_status.st_mode) &&
        identity_has_access(&uinput_status, env, S_IWUSR, S_IWGRP, S_IWOTH);
    if (!plan->readiness.uinput_accessible)
        plan->readiness.blockers |= INPUT_PROXY_DEPLOYMENT_BLOCKER_UINPUT;
    plan->readiness_available = true;
    return INPUT_PROXY_INSTALLATION_PLAN_SUCCESS;
}

const struct input_proxy_deployment_readiness *
input_proxy_installation_plan_readiness(
    const struct input_proxy_installation_plan *plan)
{
    return plan != NULL && plan->readiness_available ? &plan->readiness : NULL;
}

static bool preferred_source_choice_valid(
    enum input_proxy_preferred_source_choice choice)
{
    return choice >= INPUT_PROXY_PREFERRED_SOURCE_UNRESOLVED &&
        choice <= INPUT_PROXY_PREFERRED_SOURCE_USE_PREFERRED;
}

static bool remediation_choice_valid(enum input_proxy_remediation_choice choice)
{
    return choice >= INPUT_PROXY_REMEDIATION_UNRESOLVED &&
        choice <= INPUT_PROXY_REMEDIATION_INSTALL;
}

enum input_proxy_installation_plan_result input_proxy_installation_plan_resolve(
    struct input_proxy_installation_plan *plan,
    const struct input_proxy_deployment_choices *choices)
{
    struct input_proxy_deployment_resolution resolution = {0};
    bool libinput_ignore_resolved = true;

    if (plan == NULL || choices == NULL || !plan->readiness_available ||
        !preferred_source_choice_valid(choices->preferred_source) ||
        !remediation_choice_valid(choices->libinput_ignore)) {
        return INPUT_PROXY_INSTALLATION_PLAN_INVALID_CHOICES;
    }

    resolution.persistent_source_path = plan->source_path;
    if (plan->readiness.preferred_source_differs) {
        if (choices->preferred_source ==
            INPUT_PROXY_PREFERRED_SOURCE_USE_PREFERRED) {
            resolution.persistent_source_path = plan->preferred_source_path;
        } else if (choices->preferred_source ==
                   INPUT_PROXY_PREFERRED_SOURCE_UNRESOLVED) {
            resolution.choices_resolved = false;
        }
    } else if (choices->preferred_source ==
               INPUT_PROXY_PREFERRED_SOURCE_USE_PREFERRED) {
        return INPUT_PROXY_INSTALLATION_PLAN_INVALID_CHOICES;
    }

    if (plan->readiness.libinput_status == INPUT_PROXY_LIBINPUT_STATUS_IGNORED) {
        if (choices->libinput_ignore == INPUT_PROXY_REMEDIATION_INSTALL)
            return INPUT_PROXY_INSTALLATION_PLAN_INVALID_CHOICES;
    } else if (plan->readiness.libinput_ignore_rule_available) {
        libinput_ignore_resolved =
            choices->libinput_ignore != INPUT_PROXY_REMEDIATION_UNRESOLVED;
        resolution.libinput_ignore_action =
            choices->libinput_ignore == INPUT_PROXY_REMEDIATION_INSTALL;
    } else if (choices->libinput_ignore == INPUT_PROXY_REMEDIATION_INSTALL) {
        return INPUT_PROXY_INSTALLATION_PLAN_INVALID_CHOICES;
    }

    resolution.choices_resolved =
        (!plan->readiness.preferred_source_differs ||
         choices->preferred_source != INPUT_PROXY_PREFERRED_SOURCE_UNRESOLVED) &&
        libinput_ignore_resolved;
    resolution.application_ready = resolution.choices_resolved &&
        plan->readiness.blockers == INPUT_PROXY_DEPLOYMENT_BLOCKER_NONE;

    plan->resolution = resolution;
    plan->resolution_available = true;
    plan->readiness.selected_source_path = resolution.persistent_source_path;
    plan->config.source_path = resolution.persistent_source_path;
    return INPUT_PROXY_INSTALLATION_PLAN_SUCCESS;
}

const struct input_proxy_deployment_resolution *
input_proxy_installation_plan_resolution(
    const struct input_proxy_installation_plan *plan)
{
    return plan != NULL && plan->resolution_available
        ? &plan->resolution : NULL;
}

void input_proxy_installation_plan_release_runtime_name(
    struct input_proxy_installation_plan *plan)
{
    if (plan == NULL) {
        return;
    }
    input_proxy_instance_name_release(plan->name_ownership);
    plan->name_ownership = NULL;
}

void input_proxy_installation_plan_destroy(
    struct input_proxy_installation_plan *plan)
{
    if (plan == NULL) {
        return;
    }
    input_proxy_installation_plan_release_runtime_name(plan);
    free(plan->source_path);
    free(plan->instance_name);
    free(plan);
}
