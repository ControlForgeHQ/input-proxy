#ifndef INPUT_PROXY_DEPLOYMENT_READINESS_INTERNAL_H
#define INPUT_PROXY_DEPLOYMENT_READINESS_INTERNAL_H

#include "libinput_status_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "device_inspection_internal.h"

typedef int (*input_proxy_deployment_stat_fn)(
    const char *path,
    struct stat *status,
    void *userdata
);

enum input_proxy_deployment_blocker {
    INPUT_PROXY_DEPLOYMENT_BLOCKER_NONE = 0,
    INPUT_PROXY_DEPLOYMENT_BLOCKER_SOURCE = 1U << 0,
    INPUT_PROXY_DEPLOYMENT_BLOCKER_PACKAGE_INTEGRATION = 1U << 1,
    INPUT_PROXY_DEPLOYMENT_BLOCKER_UINPUT = 1U << 2
};

struct input_proxy_deployment_environment {
    const char *service_name;
    const char *sysfs_input_path;
    const char *device_input_path;
    const char *uinput_path;
    const char *udev_data_path;
    uid_t service_uid;
    gid_t service_gid;
    const gid_t *service_groups;
    size_t service_group_count;
    input_proxy_deployment_stat_fn stat_path;
    void *stat_userdata;
};

struct input_proxy_deployment_readiness {
    const char *supplied_source_path;
    const char *selected_source_path;
    const char *preferred_source_path;
    bool preferred_source_differs;
    bool physical_source;
    bool source_accessible;
    bool uinput_accessible;
    enum input_proxy_libinput_status libinput_status;
    bool libinput_ignore_rule_available;
    struct input_proxy_device_rule_identity rule_identity;
    unsigned int blockers;
};

#endif
