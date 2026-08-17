#ifndef INPUT_PROXY_INSTALL_COMMAND_INTERNAL_H
#define INPUT_PROXY_INSTALL_COMMAND_INTERNAL_H

#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>

#include "deployment_readiness_internal.h"

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
};

void input_proxy_install_print_help(FILE *stream);
int input_proxy_install_command(int argc, char *argv[]);
int input_proxy_install_command_with_environment(
    int argc,
    char *argv[],
    const struct input_proxy_install_command_environment *environment
);

#endif
