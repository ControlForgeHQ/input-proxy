#ifndef INPUT_PROXY_INSTALLATION_APPLICATION_INTERNAL_H
#define INPUT_PROXY_INSTALLATION_APPLICATION_INTERNAL_H

#include "installation_planner_internal.h"

#define INPUT_PROXY_UDEV_RULE_DIRECTORY "/etc/udev/rules.d"

enum input_proxy_installation_application_result {
    INPUT_PROXY_INSTALLATION_APPLICATION_SUCCESS = 0,
    INPUT_PROXY_INSTALLATION_APPLICATION_INVALID_PLAN,
    INPUT_PROXY_INSTALLATION_APPLICATION_RESPONSE_FAILED,
    INPUT_PROXY_INSTALLATION_APPLICATION_RULE_GENERATION_FAILED,
    INPUT_PROXY_INSTALLATION_APPLICATION_RULE_PUBLICATION_FAILED,
    INPUT_PROXY_INSTALLATION_APPLICATION_ROLLBACK_FAILED
};

struct input_proxy_installation_application_environment {
    const char *udev_rule_directory;
    bool inject_rule_publication_failure;
    bool inject_response_rollback_failure;
};

struct input_proxy_installation_application_failure {
    enum input_proxy_installation_application_result result;
    char rollback_path[1024];
};

enum input_proxy_installation_application_result
input_proxy_installation_plan_apply(
    const struct input_proxy_installation_plan *plan,
    const struct input_proxy_installed_instance_store *store,
    const struct input_proxy_installation_application_environment *environment,
    struct input_proxy_installation_application_failure *failure);

#endif
