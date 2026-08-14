#define _POSIX_C_SOURCE 200809L

#include "runtime_discovery_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct lookup_entry {
    const char *service_name;
    const char *source_path;
    int result;
};

struct lookup_fixture {
    const struct lookup_entry *entries;
    size_t entry_count;
    size_t calls;
};

static int fixture_lookup(
    const char *service_name, char **source_path, void *userdata)
{
    struct lookup_fixture *fixture = userdata;
    size_t index;

    fixture->calls++;
    for (index = 0; index < fixture->entry_count; ++index) {
        if (strcmp(fixture->entries[index].service_name, service_name) != 0) {
            continue;
        }
        if (fixture->entries[index].result < 0) {
            return fixture->entries[index].result;
        }
        *source_path = strdup(fixture->entries[index].source_path);
        return *source_path == NULL ? -ENOMEM : 0;
    }
    return -ENOENT;
}

static char *render_list(const struct input_proxy_runtime_snapshot *snapshot)
{
    char *output = NULL;
    size_t output_size = 0;
    FILE *stream = open_memstream(&output, &output_size);

    if (stream == NULL) {
        return NULL;
    }
    input_proxy_runtime_print_list(stream, snapshot);
    fclose(stream);
    return output;
}

static char *render_inspect(
    const struct input_proxy_runtime_snapshot *snapshot,
    const char *source_path)
{
    char *output = NULL;
    size_t output_size = 0;
    FILE *stream = open_memstream(&output, &output_size);

    if (stream == NULL) {
        return NULL;
    }
    input_proxy_runtime_print_inspect(stream, snapshot, source_path);
    fclose(stream);
    return output;
}

static int test_discovery_filtering_race_and_sorting(void)
{
    static const char *const names[] = {
        ":1.42",
        "org.example.Unrelated",
        "net.controlforge.InputProxy1.Instance.Zulu",
        "net.controlforge.InputProxy1.Instance.Vanished",
        "net.controlforge.InputProxy1.Instance.Alpha",
        "net.controlforge.InputProxy1.Instance.Legacy.FinalComponent",
        "net.controlforge.InputProxy1.Instance.",
        NULL
    };
    static const struct lookup_entry entries[] = {
        {"net.controlforge.InputProxy1.Instance.Zulu", "/dev/input/event3", 0},
        {"net.controlforge.InputProxy1.Instance.Vanished", NULL, -ENOENT},
        {"net.controlforge.InputProxy1.Instance.Alpha", "/dev/input/event3", 0},
        {"net.controlforge.InputProxy1.Instance.Legacy.FinalComponent",
            "/dev/input/event9", 0}
    };
    struct lookup_fixture fixture = {entries, 4, 0};
    struct input_proxy_runtime_snapshot snapshot;
    char *list_output;
    char *inspect_output;
    const char *alpha_position;
    const char *zulu_position;
    int failures = 0;

    if (input_proxy_runtime_snapshot_build(
            &snapshot, names, fixture_lookup, &fixture) != 0) {
        return 1;
    }
    if (!snapshot.available || snapshot.record_count != 3 ||
        fixture.calls != 4 ||
        strcmp(snapshot.records[0].instance_name, "Alpha") != 0 ||
        strcmp(snapshot.records[1].instance_name, "FinalComponent") != 0 ||
        strcmp(snapshot.records[2].instance_name, "Zulu") != 0 ||
        strcmp(snapshot.records[0].source_path, "/dev/input/event3") != 0) {
        fprintf(stderr, "runtime snapshot filtering or sorting failed\n");
        failures++;
    }

    list_output = render_list(&snapshot);
    inspect_output = render_inspect(&snapshot, "/dev/input/event3");
    alpha_position = list_output == NULL ? NULL : strstr(list_output, "Alpha");
    zulu_position = list_output == NULL ? NULL : strstr(list_output, "Zulu");
    if (list_output == NULL ||
        strstr(list_output, "RUNNING INSTANCES\n\n") == NULL ||
        strstr(list_output, "INSTANCE                       SOURCE\n") == NULL ||
        alpha_position == NULL || zulu_position == NULL ||
        alpha_position > zulu_position ||
        strstr(list_output, ":1.42") != NULL ||
        strstr(list_output, "Vanished") != NULL) {
        fprintf(stderr, "runtime list formatting failed:\n%s",
            list_output == NULL ? "(null)\n" : list_output);
        failures++;
    }
    if (inspect_output == NULL ||
        strcmp(inspect_output,
            "Running input-proxy instances:\n  Alpha\n  Zulu\n\n") != 0) {
        fprintf(stderr, "runtime inspect formatting failed:\n%s",
            inspect_output == NULL ? "(null)\n" : inspect_output);
        failures++;
    }
    free(list_output);
    free(inspect_output);
    input_proxy_runtime_snapshot_destroy(&snapshot);
    return failures;
}

static int test_empty_unavailable_and_no_match_output(void)
{
    struct input_proxy_runtime_snapshot empty = {.available = true};
    struct input_proxy_runtime_snapshot unavailable = {0};
    char *output;
    int failures = 0;

    output = render_list(&empty);
    if (output == NULL || strcmp(output, "RUNNING INSTANCES\n\nNone\n\n") != 0) {
        fprintf(stderr, "empty runtime list output failed\n");
        failures++;
    }
    free(output);
    output = render_list(&unavailable);
    if (output == NULL || strcmp(output,
            "RUNNING INSTANCES\n\nRuntime information unavailable: system "
            "D-Bus could not be queried.\n\n") != 0) {
        fprintf(stderr, "unavailable runtime list output failed\n");
        failures++;
    }
    free(output);
    output = render_inspect(&empty, "/dev/input/event0");
    if (output == NULL || output[0] != '\0') {
        fprintf(stderr, "no-match inspection was not silent\n");
        failures++;
    }
    free(output);
    output = render_inspect(&unavailable, "/dev/input/event0");
    if (output == NULL || strcmp(output,
            "Runtime instance information unavailable: system D-Bus could "
            "not be queried.\n\n") != 0) {
        fprintf(stderr, "unavailable runtime inspection output failed\n");
        failures++;
    }
    free(output);
    return failures;
}

int main(void)
{
    const int failures = test_discovery_filtering_race_and_sorting() +
        test_empty_unavailable_and_no_match_output();

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
