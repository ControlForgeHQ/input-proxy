#ifndef INPUT_PROXY_DEVICE_INSPECTION_INTERNAL_H
#define INPUT_PROXY_DEVICE_INSPECTION_INTERNAL_H

#include <input_proxy/result.h>

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/stat.h>

struct input_proxy_runtime_snapshot;

struct input_proxy_device_rule_identity {
    char vendor[32];
    char model[32];
    char path[512];
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
bool input_proxy_rule_identity_is_narrow(
    const struct input_proxy_device_rule_identity *identity);

struct input_proxy_access_remediation {
    bool source_ok;
    const char *source_path;
    const char *source_group;
    bool source_group_readable;
    bool source_group_member;
    bool uinput_exists;
    bool uinput_ok;
    const char *uinput_group;
    bool uinput_group_writable;
    bool uinput_group_member;
    bool uinput_module_loaded;
    bool input_group_available;
    const char *user;
};

void input_proxy_print_wrapped_values(
    FILE *stream,
    const char *label,
    const char *const values[],
    size_t value_count
);

void input_proxy_print_access_remediation(
    FILE *stream,
    const struct input_proxy_access_remediation *access
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
    const char *preferred_source
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

#endif
