#ifndef INPUT_PROXY_INSTALLATION_ACTIVATION_INTERNAL_H
#define INPUT_PROXY_INSTALLATION_ACTIVATION_INTERNAL_H

#include "deployment_readiness_internal.h"
#include "installation_planner_internal.h"

enum input_proxy_installation_service_state {
    INPUT_PROXY_INSTALLATION_SERVICE_RUNNING = 0,
    INPUT_PROXY_INSTALLATION_SERVICE_FAILED,
    INPUT_PROXY_INSTALLATION_SERVICE_INACTIVE,
    INPUT_PROXY_INSTALLATION_SERVICE_MANAGEMENT_FAILED
};

enum input_proxy_installation_activation_result {
    INPUT_PROXY_INSTALLATION_ACTIVATION_SUCCESS = 0,
    INPUT_PROXY_INSTALLATION_ACTIVATION_INVALID_PLAN,
    INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_RELOAD_FAILED,
    INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_TRIGGER_FAILED,
    INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_SETTLE_FAILED,
    INPUT_PROXY_INSTALLATION_ACTIVATION_PERMISSION_VERIFICATION_FAILED,
    INPUT_PROXY_INSTALLATION_ACTIVATION_LIBINPUT_VERIFICATION_FAILED,
    INPUT_PROXY_INSTALLATION_ACTIVATION_VIRTUAL_PERMISSION_VERIFICATION_FAILED,
    INPUT_PROXY_INSTALLATION_ACTIVATION_ENABLE_FAILED,
    INPUT_PROXY_INSTALLATION_ACTIVATION_START_FAILED,
    INPUT_PROXY_INSTALLATION_ACTIVATION_SERVICE_FAILED,
    INPUT_PROXY_INSTALLATION_ACTIVATION_SERVICE_INACTIVE,
    INPUT_PROXY_INSTALLATION_ACTIVATION_SERVICE_MANAGEMENT_FAILED
};

struct input_proxy_installation_activation_operations {
    bool (*reload_udev)(void *userdata);
    bool (*trigger_source)(const char *source_path, void *userdata);
    bool (*settle_udev)(void *userdata);
    bool (*verify_source_permission)(const char *source_path,
        const struct input_proxy_deployment_environment *deployment,
        void *userdata);
    bool (*verify_libinput_ignore)(const char *source_path,
        const struct input_proxy_deployment_environment *deployment,
        void *userdata);
    bool (*verify_virtual_permission)(const char *instance_name,
        const struct input_proxy_deployment_environment *deployment,
        void *userdata);
    bool (*enable_service)(const char *unit, void *userdata);
    bool (*start_service)(const char *unit, void *userdata);
    enum input_proxy_installation_service_state (*service_state)(
        const char *unit, void *userdata);
    void *userdata;
};

struct input_proxy_installation_activation_environment {
    const struct input_proxy_deployment_environment *deployment;
    const struct input_proxy_installation_activation_operations *operations;
};

enum input_proxy_installation_activation_result
input_proxy_installation_plan_activate(
    struct input_proxy_installation_plan *plan,
    const struct input_proxy_installation_activation_environment *environment
);

#endif
