#ifndef INPUT_PROXY_SERVICE_IDENTITY_INTERNAL_H
#define INPUT_PROXY_SERVICE_IDENTITY_INTERNAL_H

#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>

struct input_proxy_deployment_environment;

enum input_proxy_install_service_identity_result {
    INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID = 0,
    INPUT_PROXY_INSTALL_SERVICE_USER_MISSING,
    INPUT_PROXY_INSTALL_SERVICE_GROUP_MISSING,
    INPUT_PROXY_INSTALL_SERVICE_PRIMARY_GROUP_MISMATCH,
    INPUT_PROXY_INSTALL_SERVICE_INPUT_GROUP_MISSING,
    INPUT_PROXY_INSTALL_SERVICE_INPUT_MEMBERSHIP_MISSING,
    INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE
};

enum input_proxy_install_service_identity_result
input_proxy_service_environment_resolve(
    struct input_proxy_deployment_environment *environment,
    gid_t **owned_groups
);

bool input_proxy_deployment_identity_has_access(
    const struct stat *status,
    const struct input_proxy_deployment_environment *environment,
    mode_t owner_bit,
    mode_t group_bit,
    mode_t other_bit
);

#endif
