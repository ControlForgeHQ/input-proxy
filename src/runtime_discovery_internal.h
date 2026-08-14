#ifndef INPUT_PROXY_RUNTIME_DISCOVERY_INTERNAL_H
#define INPUT_PROXY_RUNTIME_DISCOVERY_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

struct input_proxy_runtime_record {
    char *instance_name;
    char *source_path;
};

struct input_proxy_runtime_snapshot {
    bool available;
    struct input_proxy_runtime_record *records;
    size_t record_count;
};

typedef int (*input_proxy_runtime_source_lookup)(
    const char *service_name, char **source_path, void *userdata);

void input_proxy_runtime_discover(
    struct input_proxy_runtime_snapshot *snapshot);
int input_proxy_runtime_snapshot_build(
    struct input_proxy_runtime_snapshot *snapshot,
    const char *const *service_names,
    input_proxy_runtime_source_lookup lookup,
    void *userdata);
void input_proxy_runtime_snapshot_destroy(
    struct input_proxy_runtime_snapshot *snapshot);
void input_proxy_runtime_print_list(
    FILE *stream, const struct input_proxy_runtime_snapshot *snapshot);
void input_proxy_runtime_print_inspect(
    FILE *stream, const struct input_proxy_runtime_snapshot *snapshot,
    const char *source_path);

#endif
