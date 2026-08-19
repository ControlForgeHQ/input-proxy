#ifndef INPUT_PROXY_INSTALL_COMMAND_INTERNAL_H
#define INPUT_PROXY_INSTALL_COMMAND_INTERNAL_H

#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>

#include "deployment_readiness_internal.h"
#include "installation_activation_internal.h"

enum input_proxy_install_service_identity_result {
    INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID = 0,
    INPUT_PROXY_INSTALL_SERVICE_USER_MISSING,
    INPUT_PROXY_INSTALL_SERVICE_GROUP_MISSING,
    INPUT_PROXY_INSTALL_SERVICE_PRIMARY_GROUP_MISMATCH,
    INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE
};

typedef enum input_proxy_install_service_identity_result
(*input_proxy_install_service_identity_check_fn)(void *userdata);

struct input_proxy_install_command_environment {
    uid_t effective_uid;
    bool interactive;
    FILE *input;
    FILE *output;
    FILE *error;
    const char *installed_instance_directory;
    const char *udev_rule_directory;
    bool inject_rule_publication_failure;
    bool inject_response_rollback_failure;
    const struct input_proxy_deployment_environment *deployment;
    const struct input_proxy_installation_activation_operations *activation_operations;
    input_proxy_install_service_identity_check_fn check_service_identity;
    void *service_identity_userdata;
};

void input_proxy_install_print_help(FILE *stream);
int input_proxy_install_command(int argc, char *argv[]);
int input_proxy_install_command_with_environment(
    int argc,
    char *argv[],
    const struct input_proxy_install_command_environment *environment
);

#endif
