#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "service_identity_internal.h"
#include "deployment_readiness_internal.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdlib.h>

#define SERVICE_IDENTITY "input-proxy"

static bool group_matches(
    const struct input_proxy_deployment_environment *environment,
    gid_t group)
{
    size_t index;

    if (environment->service_gid == group) return true;
    for (index = 0; index < environment->service_group_count; ++index) {
        if (environment->service_groups[index] == group) return true;
    }
    return false;
}

bool input_proxy_deployment_identity_has_access(
    const struct stat *status,
    const struct input_proxy_deployment_environment *environment,
    mode_t owner_bit,
    mode_t group_bit,
    mode_t other_bit)
{
    if (status == NULL || environment == NULL) return false;
    if (environment->service_uid == 0) return true;
    if (environment->service_uid == status->st_uid)
        return (status->st_mode & owner_bit) != 0;
    if (group_matches(environment, status->st_gid))
        return (status->st_mode & group_bit) != 0;
    return (status->st_mode & other_bit) != 0;
}

enum input_proxy_install_service_identity_result
input_proxy_service_environment_resolve(
    struct input_proxy_deployment_environment *environment,
    gid_t **owned_groups)
{
    struct passwd *account;
    struct group *group;
    struct group *input_group;
    int count = 0;
    int index;
    bool input_member = false;

    if (environment == NULL || owned_groups == NULL)
        return INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE;
    *owned_groups = NULL;
    errno = 0;
    account = getpwnam(SERVICE_IDENTITY);
    if (account == NULL)
        return errno == 0 ? INPUT_PROXY_INSTALL_SERVICE_USER_MISSING
                          : INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE;
    errno = 0;
    group = getgrnam(SERVICE_IDENTITY);
    if (group == NULL)
        return errno == 0 ? INPUT_PROXY_INSTALL_SERVICE_GROUP_MISSING
                          : INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE;
    if (account->pw_gid != group->gr_gid)
        return INPUT_PROXY_INSTALL_SERVICE_PRIMARY_GROUP_MISMATCH;
    errno = 0;
    input_group = getgrnam("input");
    if (input_group == NULL)
        return errno == 0 ? INPUT_PROXY_INSTALL_SERVICE_INPUT_GROUP_MISSING
                          : INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE;
    (void)getgrouplist(SERVICE_IDENTITY, account->pw_gid, NULL, &count);
    if (count <= 0) count = 1;
    *owned_groups = malloc((size_t)count * sizeof(**owned_groups));
    if (*owned_groups == NULL)
        return INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE;
    if (getgrouplist(SERVICE_IDENTITY, account->pw_gid, *owned_groups, &count) < 0) {
        free(*owned_groups);
        *owned_groups = NULL;
        return INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE;
    }
    for (index = 0; index < count; ++index) {
        if ((*owned_groups)[index] == input_group->gr_gid) input_member = true;
    }
    if (!input_member) {
        free(*owned_groups);
        *owned_groups = NULL;
        return INPUT_PROXY_INSTALL_SERVICE_INPUT_MEMBERSHIP_MISSING;
    }
    *environment = (struct input_proxy_deployment_environment) {
        .sysfs_input_path = "/sys/class/input",
        .device_input_path = "/dev/input",
        .uinput_path = "/dev/uinput",
        .udev_data_path = "/run/udev/data",
        .service_uid = account->pw_uid,
        .service_gid = account->pw_gid,
        .service_groups = *owned_groups,
        .service_group_count = (size_t)count
    };
    return INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID;
}
