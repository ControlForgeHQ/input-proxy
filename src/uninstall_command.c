#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "uninstall_command_internal.h"

#include "installed_instance_internal.h"
#include "response_file_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define INPUT_PROXY_UDEV_RULE_DIRECTORY "/etc/udev/rules.d"

static int run_command(char *const arguments[])
{
    pid_t child = fork(), waited;
    int status = 0;
    if (child < 0) return -1;
    if (child == 0) { execvp(arguments[0], arguments); _exit(127); }
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (waited != child || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

static enum input_proxy_uninstall_stage_result default_stop(
    const char *unit, void *userdata)
{
    char *check[] = { "systemctl", "is-active", "--quiet", (char *)unit, NULL };
    char *stop[] = { "systemctl", "stop", (char *)unit, NULL };
    int status;
    (void)userdata;
    status = run_command(check);
    if (status == 3) return INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED;
    if (status != 0) return INPUT_PROXY_UNINSTALL_STAGE_FAILED;
    return run_command(stop) == 0 ? INPUT_PROXY_UNINSTALL_STAGE_SUCCESS
                                  : INPUT_PROXY_UNINSTALL_STAGE_FAILED;
}

static enum input_proxy_uninstall_stage_result default_disable(
    const char *unit, void *userdata)
{
    char *check[] = { "systemctl", "is-enabled", "--quiet", (char *)unit, NULL };
    char *disable[] = { "systemctl", "disable", (char *)unit, NULL };
    int status;
    (void)userdata;
    status = run_command(check);
    if (status == 1) return INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED;
    if (status != 0) return INPUT_PROXY_UNINSTALL_STAGE_FAILED;
    return run_command(disable) == 0 ? INPUT_PROXY_UNINSTALL_STAGE_SUCCESS
                                     : INPUT_PROXY_UNINSTALL_STAGE_FAILED;
}

static enum input_proxy_uninstall_stage_result default_remove(
    const char *path, void *userdata)
{
    (void)userdata;
    if (unlink(path) == 0) return INPUT_PROXY_UNINSTALL_STAGE_SUCCESS;
    return errno == ENOENT ? INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED
                           : INPUT_PROXY_UNINSTALL_STAGE_FAILED;
}

static bool default_source_present(const char *path, void *userdata)
{
    struct stat status;
    (void)userdata;
    return stat(path, &status) == 0;
}

static bool default_reload(void *userdata)
{
    char *arguments[] = { "udevadm", "control", "--reload-rules", NULL };
    (void)userdata;
    return run_command(arguments) == 0;
}

static bool source_sysfs_path(const char *source_path, char *path, size_t size)
{
    char resolved[4096];
    const char *name;
    if (realpath(source_path, resolved) == NULL) return false;
    name = strrchr(resolved, '/');
    name = name == NULL ? resolved : name + 1;
    return name[0] != '\0' &&
        snprintf(path, size, "/sys/class/input/%s", name) < (int)size;
}

static bool default_trigger(const char *source_path, void *userdata)
{
    char path[4096];
    char *arguments[] = { "udevadm", "trigger", "--action=change", path, NULL };
    (void)userdata;
    return source_sysfs_path(source_path, path, sizeof(path)) &&
        run_command(arguments) == 0;
}

static bool default_settle(void *userdata)
{
    char *arguments[] = { "udevadm", "settle", NULL };
    (void)userdata;
    return run_command(arguments) == 0;
}

static const struct input_proxy_uninstall_operations default_operations = {
    .stop_service = default_stop, .disable_service = default_disable,
    .remove_file = default_remove, .source_present = default_source_present,
    .reload_udev = default_reload, .trigger_source = default_trigger,
    .settle_udev = default_settle
};

void input_proxy_uninstall_print_help(FILE *stream)
{
    fputs("Remove one persistent input-proxy Installed Instance.\n\n"
        "Usage:\n  input-proxy uninstall [INSTANCE_NAME]\n\n"
        "When INSTANCE_NAME is omitted, an interactive selection is made from\n"
        "the authoritative Installed Instance response artifacts.\n\n"
        "Options:\n  --help  Show this help and exit.\n\n", stream);
}

static char *select_instance(const struct input_proxy_installed_instance_list *list,
    const struct input_proxy_uninstall_command_environment *environment)
{
    char *line = NULL, *end;
    size_t capacity = 0, index;
    unsigned long selection;
    ssize_t length;
    if (!environment->interactive) {
        fprintf(environment->error, "input-proxy: Installed Instance selection is required, but standard input is not interactive\n");
        return NULL;
    }
    fputs("Installed Instances:\n", environment->output);
    for (index = 0; index < list->count; ++index)
        fprintf(environment->output, "  %zu) %s\n", index + 1, list->names[index]);
    fputs("Select an Installed Instance: ", environment->output);
    fflush(environment->output);
    length = getline(&line, &capacity, environment->input);
    if (length < 0) { free(line); return NULL; }
    errno = 0;
    selection = strtoul(line, &end, 10);
    while (*end == ' ' || *end == '\t') end++;
    if (errno != 0 || end == line || (*end != '\n' && *end != '\0') ||
        selection == 0 || selection > list->count) {
        fprintf(environment->error, "input-proxy: invalid Installed Instance selection\n");
        free(line);
        return NULL;
    }
    free(line);
    return strdup(list->names[selection - 1]);
}

static const char *source_from_response(int argc, char **argv)
{
    int index;
    for (index = 2; index + 1 < argc; ++index)
        if (strcmp(argv[index], "--source") == 0) return argv[index + 1];
    return NULL;
}

static void report_stage(FILE *output, FILE *error, const char *stage,
    enum input_proxy_uninstall_stage_result result, const char *not_required)
{
    if (result == INPUT_PROXY_UNINSTALL_STAGE_SUCCESS)
        fprintf(output, "  %s: succeeded\n", stage);
    else if (result == INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED)
        fprintf(output, "  %s: %s\n", stage, not_required);
    else
        fprintf(error, "input-proxy: %s failed\n", stage);
}

int input_proxy_uninstall_command_with_environment(int argc, char *argv[],
    const struct input_proxy_uninstall_command_environment *environment)
{
    const struct input_proxy_uninstall_operations *ops;
    struct input_proxy_installed_instance_store *store = NULL;
    struct input_proxy_installed_instance_list list = { 0 };
    char *name = NULL, *response_path = NULL, *rule_path = NULL;
    char **response_argv = NULL;
    int response_argc = 0;
    const char *source = NULL;
    char unit[256];
    bool exists = false, failed = false;
    enum input_proxy_installed_instance_result store_result;
    enum input_proxy_uninstall_stage_result stop_result, disable_result;
    enum input_proxy_uninstall_stage_result rule_result, response_result;
    const char *rule_directory;
    size_t rule_size;

    if (environment == NULL || environment->input == NULL ||
        environment->output == NULL || environment->error == NULL) return EXIT_FAILURE;
    if (environment->effective_uid != 0) {
        fputs("input-proxy: uninstall requires root privileges; rerun this command with sudo\n\n", environment->error);
        return EXIT_FAILURE;
    }
    if (argc > 3 || (argc == 3 && strcmp(argv[2], "--help") == 0)) {
        fputs("input-proxy: invalid uninstall arguments\n", environment->error);
        input_proxy_uninstall_print_help(environment->error);
        return EXIT_FAILURE;
    }
    store_result = environment->installed_instance_directory == NULL
        ? input_proxy_installed_instance_store_create(&store)
        : input_proxy_installed_instance_store_create_for_directory(&store,
            environment->installed_instance_directory);
    if (store_result != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
        fputs("input-proxy: failed to initialize the Installed Instance store\n", environment->error);
        goto cleanup_with_spacing;
    }
    if (argc == 3) {
        name = strdup(argv[2]);
        if (name == NULL) goto cleanup;
        store_result = input_proxy_installed_instance_exists(store, name, &exists);
        if (store_result == INPUT_PROXY_INSTALLED_INSTANCE_INVALID_NAME) {
            fprintf(environment->error, "input-proxy: invalid Instance Name '%s'\n", name);
            goto cleanup_with_spacing;
        }
        if (store_result != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
            fputs("input-proxy: failed to inspect the Installed Instance store\n", environment->error);
            goto cleanup_with_spacing;
        }
        if (!exists) {
            fprintf(environment->error, "input-proxy: Installed Instance '%s' does not exist\n", name);
            goto cleanup_with_spacing;
        }
    } else {
        store_result = input_proxy_installed_instance_enumerate(store, &list);
        if (store_result != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
            fputs("input-proxy: failed to enumerate Installed Instances\n", environment->error);
            goto cleanup_with_spacing;
        }
        if (list.count == 0) {
            fputs("No Installed Instances are present.\n\n", environment->output);
            input_proxy_installed_instance_list_destroy(&list);
            input_proxy_installed_instance_store_destroy(store);
            return EXIT_SUCCESS;
        }
        name = select_instance(&list, environment);
        if (name == NULL) goto cleanup_with_spacing;
    }
    if (input_proxy_installed_instance_path(store, name, &response_path) !=
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) goto cleanup;
    rule_directory = environment->udev_rule_directory != NULL
        ? environment->udev_rule_directory : INPUT_PROXY_UDEV_RULE_DIRECTORY;
    rule_size = strlen(rule_directory) + strlen(name) + 32;
    rule_path = malloc(rule_size);
    if (rule_path == NULL) goto cleanup;
    (void)snprintf(rule_path, rule_size, "%s/90-input-proxy-%s.rules", rule_directory, name);
    if (snprintf(unit, sizeof(unit), "input-proxy@%s.service", name) >= (int)sizeof(unit)) goto cleanup;
    ops = environment->operations == NULL ? &default_operations : environment->operations;
    if (ops->stop_service == NULL || ops->disable_service == NULL ||
        ops->remove_file == NULL) goto cleanup;

    fprintf(environment->output, "Uninstalling Installed Instance '%s':\n", name);
    stop_result = ops->stop_service(unit, ops->userdata);
    report_stage(environment->output, environment->error, "Service stop", stop_result, "already inactive");
    failed |= stop_result == INPUT_PROXY_UNINSTALL_STAGE_FAILED;
    disable_result = ops->disable_service(unit, ops->userdata);
    report_stage(environment->output, environment->error, "Service disable", disable_result, "already disabled");
    failed |= disable_result == INPUT_PROXY_UNINSTALL_STAGE_FAILED;

    (void)input_proxy_response_file_read(response_path, argv[0], &response_argv, &response_argc);
    source = source_from_response(response_argc, response_argv);
    rule_result = ops->remove_file(rule_path, ops->userdata);
    report_stage(environment->output, environment->error, "Udev rule removal", rule_result, "rule not present");
    failed |= rule_result == INPUT_PROXY_UNINSTALL_STAGE_FAILED;
    if (rule_result == INPUT_PROXY_UNINSTALL_STAGE_SUCCESS) {
        if (ops->reload_udev == NULL || ops->source_present == NULL ||
            ops->trigger_source == NULL || ops->settle_udev == NULL) {
            fputs("input-proxy: Udev activation operations are unavailable\n",
                environment->error);
            failed = true;
        } else if (!ops->reload_udev(ops->userdata)) {
            fputs("input-proxy: Udev rule reload failed\n", environment->error);
            failed = true;
        } else if (source != NULL && ops->source_present(source, ops->userdata)) {
            if (!ops->trigger_source(source, ops->userdata)) {
                fputs("input-proxy: Targeted Physical Source udev trigger failed\n", environment->error);
                failed = true;
            } else if (!ops->settle_udev(ops->userdata)) {
                fputs("input-proxy: Udev settle failed\n", environment->error);
                failed = true;
            } else {
                fputs("  Udev activation: rules reloaded; Physical Source retriggered and settled\n", environment->output);
            }
        } else {
            fputs("  Udev activation: rules reloaded; Physical Source unavailable, no device retriggered\n", environment->output);
        }
    } else if (rule_result == INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED) {
        fputs("  Udev activation: not required\n", environment->output);
    }
    if (failed) {
        fputs("  Response artifact removal: retained for retry\n", environment->output);
        fprintf(environment->error, "input-proxy: uninstall of '%s' is incomplete; the response artifact remains authoritative and the command can be retried\n", name);
        goto cleanup_with_spacing;
    }
    response_result = ops->remove_file(response_path, ops->userdata);
    if (response_result != INPUT_PROXY_UNINSTALL_STAGE_SUCCESS) {
        report_stage(environment->output, environment->error,
            "Response artifact removal", response_result, "not present");
        goto cleanup_with_spacing;
    }
    report_stage(environment->output, environment->error,
        "Response artifact removal", response_result, "not present");
    fprintf(environment->output, "Installed Instance '%s' has been uninstalled.\n\n", name);
    input_proxy_response_file_free(response_argc, response_argv);
    free(rule_path); free(response_path); free(name);
    input_proxy_installed_instance_list_destroy(&list);
    input_proxy_installed_instance_store_destroy(store);
    return EXIT_SUCCESS;

cleanup_with_spacing:
    fputc('\n', environment->error);
cleanup:
    input_proxy_response_file_free(response_argc, response_argv);
    free(rule_path); free(response_path); free(name);
    input_proxy_installed_instance_list_destroy(&list);
    input_proxy_installed_instance_store_destroy(store);
    return EXIT_FAILURE;
}

int input_proxy_uninstall_command(int argc, char *argv[])
{
    const struct input_proxy_uninstall_command_environment environment = {
        .effective_uid = geteuid(), .interactive = isatty(STDIN_FILENO),
        .input = stdin, .output = stdout, .error = stderr
    };
    return input_proxy_uninstall_command_with_environment(argc, argv, &environment);
}
