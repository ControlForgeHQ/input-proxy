#ifndef INPUT_PROXY_DEVICE_INSPECTION_INTERNAL_H
#define INPUT_PROXY_DEVICE_INSPECTION_INTERNAL_H

#include <input_proxy/result.h>

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/stat.h>

#include "service_identity_internal.h"

struct input_proxy_runtime_snapshot;
struct input_proxy_device_identity;
struct input_proxy_installed_instance_store;

struct input_proxy_device_rule_identity {
    char udev_vendor[32];
    char udev_model[32];
    char path[512];
    char bus[16];
    char vendor[16];
    char product[16];
};

bool input_proxy_find_persistent_input_path(const char *device_input_path,
    const struct stat *status, char *persistent_path, size_t path_size);
bool input_proxy_resolve_event_node(const char *sysfs_input_path,
    const char *device_input_path, const struct stat *device_status,
    char *event_node, size_t event_node_size, char *event_sysfs_path,
    size_t sysfs_path_size);
bool input_proxy_rule_value_is_safe(const char *value);
void input_proxy_collect_rule_identity(const char *name, const char *value,
    void *userdata);
void input_proxy_rule_identity_add_kernel_identity(
    struct input_proxy_device_rule_identity *rule_identity,
    const struct input_proxy_device_identity *device_identity);
bool input_proxy_rule_identity_has_udev_identity(
    const struct input_proxy_device_rule_identity *identity);
bool input_proxy_rule_identity_is_narrow(
    const struct input_proxy_device_rule_identity *identity);
char *input_proxy_render_libinput_ignore_rule(
    const struct input_proxy_device_rule_identity *identity);

struct input_proxy_access_diagnostics {
    bool current_source_ok;
    bool current_uinput_ok;
    bool uinput_exists;
    enum input_proxy_install_service_identity_result service_identity_result;
    bool service_source_ok;
    bool service_uinput_ok;
};

void input_proxy_print_wrapped_values(
    FILE *stream,
    const char *label,
    const char *const values[],
    size_t value_count
);

void input_proxy_print_access_diagnostics(
    FILE *stream,
    const struct input_proxy_access_diagnostics *access
);

bool input_proxy_should_suggest_run(
    bool source_accessible,
    bool uinput_accessible,
    size_t associated_instance_count
);

void input_proxy_print_runtime_associations(
    FILE *stream,
    const struct input_proxy_runtime_snapshot *snapshot,
    const char *event_node,
    const char *preferred_source,
    const struct input_proxy_installed_instance_store *installed_instances
);

enum input_proxy_result input_proxy_inspect_device(
    FILE *stream,
    FILE *error_stream,
    const char *device_path,
    const char *sysfs_input_path,
    const char *device_input_path,
    const char *uinput_path,
    const char *udev_data_path,
    const struct input_proxy_runtime_snapshot *runtime_snapshot
);

enum input_proxy_result input_proxy_inspect_device_with_service_environment(
    FILE *stream,
    FILE *error_stream,
    const char *device_path,
    const char *sysfs_input_path,
    const char *device_input_path,
    const char *uinput_path,
    const char *udev_data_path,
    const struct input_proxy_runtime_snapshot *runtime_snapshot,
    enum input_proxy_install_service_identity_result service_identity_result,
    const struct input_proxy_deployment_environment *service_environment,
    const struct input_proxy_installed_instance_store *installed_instances
);

#endif
