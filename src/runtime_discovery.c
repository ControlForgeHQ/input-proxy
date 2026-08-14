#define _POSIX_C_SOURCE 200809L

#include "runtime_discovery_internal.h"
#include "runtime_dbus_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

static int compare_records(const void *left, const void *right)
{
    const struct input_proxy_runtime_record *left_record = left;
    const struct input_proxy_runtime_record *right_record = right;

    return strcmp(left_record->instance_name, right_record->instance_name);
}

void input_proxy_runtime_snapshot_destroy(
    struct input_proxy_runtime_snapshot *snapshot)
{
    size_t index;

    if (snapshot == NULL) {
        return;
    }
    for (index = 0; index < snapshot->record_count; ++index) {
        free(snapshot->records[index].instance_name);
        free(snapshot->records[index].source_path);
    }
    free(snapshot->records);
    *snapshot = (struct input_proxy_runtime_snapshot) {0};
}

int input_proxy_runtime_snapshot_build(
    struct input_proxy_runtime_snapshot *snapshot,
    const char *const *service_names,
    input_proxy_runtime_source_lookup lookup,
    void *userdata)
{
    const size_t prefix_length = strlen(INPUT_PROXY_DBUS_SERVICE_PREFIX);
    size_t index;

    if (snapshot == NULL || service_names == NULL || lookup == NULL) {
        return -EINVAL;
    }
    *snapshot = (struct input_proxy_runtime_snapshot) {.available = true};

    for (index = 0; service_names[index] != NULL; ++index) {
        const char *service_name = service_names[index];
        const char *instance_name;
        struct input_proxy_runtime_record *records;
        char *source_path = NULL;
        char *instance_copy;

        if (strncmp(service_name, INPUT_PROXY_DBUS_SERVICE_PREFIX,
                prefix_length) != 0 || service_name[prefix_length] == '\0') {
            continue;
        }
        instance_name = strrchr(service_name, '.');
        if (instance_name == NULL || instance_name[1] == '\0') {
            continue;
        }
        instance_name++;
        if (lookup(service_name, &source_path, userdata) < 0 ||
            source_path == NULL) {
            free(source_path);
            continue;
        }
        instance_copy = strdup(instance_name);
        if (instance_copy == NULL) {
            free(source_path);
            input_proxy_runtime_snapshot_destroy(snapshot);
            return -ENOMEM;
        }
        records = realloc(snapshot->records,
            (snapshot->record_count + 1) * sizeof(*records));
        if (records == NULL) {
            free(instance_copy);
            free(source_path);
            input_proxy_runtime_snapshot_destroy(snapshot);
            return -ENOMEM;
        }
        snapshot->records = records;
        snapshot->records[snapshot->record_count++] =
            (struct input_proxy_runtime_record) {
                .instance_name = instance_copy,
                .source_path = source_path
            };
    }

    if (snapshot->record_count > 1) {
        qsort(snapshot->records, snapshot->record_count,
            sizeof(*snapshot->records), compare_records);
    }
    return 0;
}

struct bus_lookup_context {
    sd_bus *bus;
};

static void free_string_vector(char **strings)
{
    size_t index;

    if (strings == NULL) {
        return;
    }
    for (index = 0; strings[index] != NULL; ++index) {
        free(strings[index]);
    }
    free(strings);
}

static int lookup_source(
    const char *service_name, char **source_path, void *userdata)
{
    const struct bus_lookup_context *context = userdata;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int result;

    result = sd_bus_get_property_string(context->bus, service_name,
        INPUT_PROXY_DBUS_OBJECT_PATH, INPUT_PROXY_DBUS_INTERFACE_NAME,
        "Source", &error, source_path);
    sd_bus_error_free(&error);
    return result;
}

void input_proxy_runtime_discover(struct input_proxy_runtime_snapshot *snapshot)
{
    struct bus_lookup_context context = {0};
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    char **service_names = NULL;
    int result;

    if (snapshot == NULL) {
        return;
    }
    *snapshot = (struct input_proxy_runtime_snapshot) {0};
    result = sd_bus_open_system(&context.bus);
    if (result < 0) {
        goto cleanup;
    }
    result = sd_bus_call_method(context.bus, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames",
        &error, &reply, "");
    if (result < 0) {
        goto cleanup;
    }
    result = sd_bus_message_read_strv(reply, &service_names);
    if (result < 0) {
        goto cleanup;
    }
    result = input_proxy_runtime_snapshot_build(snapshot,
        (const char *const *)service_names, lookup_source, &context);
    if (result < 0) {
        input_proxy_runtime_snapshot_destroy(snapshot);
    }

cleanup:
    free_string_vector(service_names);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    sd_bus_flush_close_unref(context.bus);
}

void input_proxy_runtime_print_list(
    FILE *stream, const struct input_proxy_runtime_snapshot *snapshot)
{
    size_t index;

    if (stream == NULL || snapshot == NULL) {
        return;
    }
    fprintf(stream, "%-30s %s\n", "RUNNING PROXY INSTANCES", "SOURCE");
    if (!snapshot->available) {
        fputs("Runtime information unavailable: system D-Bus could not be "
            "queried.\n\n", stream);
        return;
    }
    if (snapshot->record_count == 0) {
        fputs("None\n\n", stream);
        return;
    }
    for (index = 0; index < snapshot->record_count; ++index) {
        fprintf(stream, "%-30s %s\n",
            snapshot->records[index].instance_name,
            snapshot->records[index].source_path);
    }
    fputc('\n', stream);
}

static bool record_matches_device(
    const struct input_proxy_runtime_record *record,
    const char *event_node,
    const char *preferred_source)
{
    return strcmp(record->source_path, event_node) == 0 ||
        (preferred_source != NULL && preferred_source[0] != '\0' &&
         strcmp(record->source_path, preferred_source) == 0);
}

size_t input_proxy_runtime_association_count(
    const struct input_proxy_runtime_snapshot *snapshot,
    const char *event_node,
    const char *preferred_source)
{
    size_t index;
    size_t count = 0;

    if (snapshot == NULL || !snapshot->available || event_node == NULL) {
        return 0;
    }
    for (index = 0; index < snapshot->record_count; ++index) {
        if (record_matches_device(
                &snapshot->records[index], event_node, preferred_source)) {
            count++;
        }
    }
    return count;
}

void input_proxy_runtime_print_inspect(
    FILE *stream, const struct input_proxy_runtime_snapshot *snapshot,
    const char *event_node, const char *preferred_source)
{
    size_t index;
    bool heading_printed = false;

    if (stream == NULL || snapshot == NULL || event_node == NULL) {
        return;
    }
    if (!snapshot->available) {
        fputs("Runtime instance information unavailable: system D-Bus could "
            "not be queried.\n\n", stream);
        return;
    }
    for (index = 0; index < snapshot->record_count; ++index) {
        if (!record_matches_device(
                &snapshot->records[index], event_node, preferred_source)) {
            continue;
        }
        if (!heading_printed) {
            fputs("Running input-proxy instances:\n", stream);
            heading_printed = true;
        }
        fprintf(stream, "  %s [%s]\n",
            snapshot->records[index].instance_name,
            snapshot->records[index].source_path);
    }
    if (heading_printed) {
        fputc('\n', stream);
    }
}
