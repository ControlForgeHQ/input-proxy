#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "installation_activation_internal.h"

#include "libinput_status_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define VIRTUAL_OUTPUT_ATTEMPTS 50
#define VIRTUAL_OUTPUT_RETRY_NANOSECONDS 100000000L

static bool run_command(char *const arguments[])
{
    pid_t child = fork();
    int status = 0;
    pid_t waited;

    if (child < 0) return false;
    if (child == 0) {
        execvp(arguments[0], arguments);
        _exit(127);
    }
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool default_reload_udev(void *userdata)
{
    char *arguments[] = {"udevadm", "control", "--reload-rules", NULL};
    (void)userdata;
    return run_command(arguments);
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

static bool default_trigger_source(const char *source_path, void *userdata)
{
    char sysfs_path[4096];
    char *arguments[] = {"udevadm", "trigger", "--action=change",
        sysfs_path, NULL};
    (void)userdata;
    if (!source_sysfs_path(source_path, sysfs_path, sizeof(sysfs_path)))
        return false;
    return run_command(arguments);
}

static bool default_settle_udev(void *userdata)
{
    char *arguments[] = {"udevadm", "settle", NULL};
    (void)userdata;
    return run_command(arguments);
}

static int deployment_stat(
    const struct input_proxy_deployment_environment *deployment,
    const char *path, struct stat *status)
{
    return deployment->stat_path == NULL
        ? stat(path, status)
        : deployment->stat_path(path, status, deployment->stat_userdata);
}

static bool default_verify_source_permission(const char *source_path,
    const struct input_proxy_deployment_environment *deployment, void *userdata)
{
    pid_t child;
    pid_t waited;
    int status = 0;
    (void)userdata;
    if (deployment == NULL) return false;
    child = fork();
    if (child < 0) return false;
    if (child == 0) {
        int descriptor;
        if (setgroups(deployment->service_group_count,
                deployment->service_groups) != 0 ||
            setgid(deployment->service_gid) != 0 ||
            setuid(deployment->service_uid) != 0)
            _exit(1);
        descriptor = open(source_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (descriptor < 0) _exit(1);
        close(descriptor);
        _exit(0);
    }
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool default_verify_libinput_ignore(const char *source_path,
    const struct input_proxy_deployment_environment *deployment, void *userdata)
{
    struct stat status;
    (void)userdata;
    return deployment != NULL && deployment->udev_data_path != NULL &&
        deployment_stat(deployment, source_path, &status) == 0 &&
        input_proxy_read_libinput_status(deployment->udev_data_path, &status,
            NULL, NULL) == INPUT_PROXY_LIBINPUT_STATUS_IGNORED;
}

static bool read_name(const char *path, const char *expected)
{
    char name[256];
    FILE *file = fopen(path, "r");
    size_t length;

    if (file == NULL || fgets(name, sizeof(name), file) == NULL) {
        if (file != NULL) fclose(file);
        return false;
    }
    fclose(file);
    length = strlen(name);
    while (length > 0 && (name[length - 1] == '\n' || name[length - 1] == '\r'))
        name[--length] = '\0';
    return strcmp(name, expected) == 0;
}

static bool find_virtual_event_node(const char *instance_name,
    const struct input_proxy_deployment_environment *deployment,
    char *event_node, size_t size)
{
    DIR *directory;
    struct dirent *entry;
    bool found = false;

    if (deployment == NULL || deployment->sysfs_input_path == NULL ||
        deployment->device_input_path == NULL)
        return false;
    directory = opendir(deployment->sysfs_input_path);
    if (directory == NULL) return false;
    while ((entry = readdir(directory)) != NULL) {
        char device_path[4096];
        char resolved[4096];
        char name_path[4096];

        if (strncmp(entry->d_name, "event", 5) != 0) continue;
        if (snprintf(device_path, sizeof(device_path), "%s/%s/device",
                deployment->sysfs_input_path, entry->d_name) >=
                (int)sizeof(device_path) ||
            realpath(device_path, resolved) == NULL ||
            strncmp(resolved, "/sys/devices/virtual/input/",
                sizeof("/sys/devices/virtual/input/") - 1) != 0 ||
            snprintf(name_path, sizeof(name_path), "%s/name", device_path) >=
                (int)sizeof(name_path) ||
            !read_name(name_path, instance_name))
            continue;
        if (snprintf(event_node, size, "%s/%s",
                deployment->device_input_path, entry->d_name) >= (int)size)
            continue;
        found = true;
        break;
    }
    closedir(directory);
    return found;
}

static bool default_source_available(const char *source_path,
    const struct input_proxy_deployment_environment *deployment, void *userdata)
{
    struct stat status;
    (void)userdata;
    return deployment != NULL &&
        deployment_stat(deployment, source_path, &status) == 0 &&
        S_ISCHR(status.st_mode);
}

static enum input_proxy_virtual_output_status
default_verify_virtual_permission(const char *instance_name,
    const struct input_proxy_deployment_environment *deployment, void *userdata)
{
    char event_node[4096];
    (void)userdata;
    if (!find_virtual_event_node(instance_name, deployment, event_node,
            sizeof(event_node)))
        return INPUT_PROXY_VIRTUAL_OUTPUT_NOT_FOUND;
    return default_verify_source_permission(event_node, deployment, NULL)
        ? INPUT_PROXY_VIRTUAL_OUTPUT_READABLE
        : INPUT_PROXY_VIRTUAL_OUTPUT_UNREADABLE;
}

static void default_wait_virtual_output(void *userdata)
{
    struct timespec interval = {
        .tv_nsec = VIRTUAL_OUTPUT_RETRY_NANOSECONDS
    };
    (void)userdata;
    while (nanosleep(&interval, &interval) != 0 && errno == EINTR) {}
}

static bool default_enable_service(const char *unit, void *userdata)
{
    char *arguments[] = {"systemctl", "enable", (char *)unit, NULL};
    (void)userdata;
    return run_command(arguments);
}

static bool default_start_service(const char *unit, void *userdata)
{
    char *arguments[] = {"systemctl", "start", (char *)unit, NULL};
    (void)userdata;
    return run_command(arguments);
}

static enum input_proxy_installation_service_state default_service_state(
    const char *unit, void *userdata)
{
    int descriptors[2];
    pid_t child;
    int status = 0;
    pid_t waited;
    char output[128];
    ssize_t length;
    char *arguments[] = {"systemctl", "show", "--property=ActiveState",
        "--property=SubState", "--value", (char *)unit, NULL};
    (void)userdata;

    if (pipe(descriptors) != 0) return INPUT_PROXY_INSTALLATION_SERVICE_MANAGEMENT_FAILED;
    child = fork();
    if (child < 0) { close(descriptors[0]); close(descriptors[1]); return INPUT_PROXY_INSTALLATION_SERVICE_MANAGEMENT_FAILED; }
    if (child == 0) {
        close(descriptors[0]);
        if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(127);
        close(descriptors[1]);
        execvp(arguments[0], arguments);
        _exit(127);
    }
    close(descriptors[1]);
    length = read(descriptors[0], output, sizeof(output) - 1);
    close(descriptors[0]);
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (length < 0 || waited != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return INPUT_PROXY_INSTALLATION_SERVICE_MANAGEMENT_FAILED;
    output[length] = '\0';
    if (strncmp(output, "active\nrunning\n", 15) == 0)
        return INPUT_PROXY_INSTALLATION_SERVICE_RUNNING;
    if (strncmp(output, "failed\n", 7) == 0)
        return INPUT_PROXY_INSTALLATION_SERVICE_FAILED;
    return INPUT_PROXY_INSTALLATION_SERVICE_INACTIVE;
}

static const struct input_proxy_installation_activation_operations default_operations = {
    .reload_udev = default_reload_udev,
    .trigger_source = default_trigger_source,
    .settle_udev = default_settle_udev,
    .verify_source_permission = default_verify_source_permission,
    .verify_libinput_ignore = default_verify_libinput_ignore,
    .source_available = default_source_available,
    .verify_virtual_permission = default_verify_virtual_permission,
    .wait_virtual_output = default_wait_virtual_output,
    .enable_service = default_enable_service,
    .start_service = default_start_service,
    .service_state = default_service_state
};

enum input_proxy_installation_activation_result
input_proxy_installation_plan_activate(struct input_proxy_installation_plan *plan,
    const struct input_proxy_installation_activation_environment *environment)
{
    const struct input_proxy_installation_activation_operations *operations =
        environment != NULL && environment->operations != NULL
            ? environment->operations : &default_operations;
    const struct input_proxy_deployment_environment *deployment =
        environment == NULL ? NULL : environment->deployment;
    const struct input_proxy_deployment_resolution *resolution =
        input_proxy_installation_plan_resolution(plan);
    const struct input_proxy_deployment_readiness *readiness =
        input_proxy_installation_plan_readiness(plan);
    const struct input_proxy_session_config *config =
        input_proxy_installation_plan_config(plan);
    char unit[256];
    enum input_proxy_installation_service_state state;
    enum input_proxy_virtual_output_status virtual_status;
    unsigned int attempt;
    bool has_source_remediation;

    if (resolution == NULL || readiness == NULL ||
        readiness->supplied_source_path == NULL || config == NULL ||
        !resolution->application_ready ||
        operations->reload_udev == NULL || operations->settle_udev == NULL ||
        operations->source_available == NULL ||
        operations->verify_virtual_permission == NULL ||
        operations->wait_virtual_output == NULL ||
        operations->enable_service == NULL || operations->start_service == NULL ||
        operations->service_state == NULL)
        return INPUT_PROXY_INSTALLATION_ACTIVATION_INVALID_PLAN;
    has_source_remediation = resolution->source_permission_action ||
        resolution->libinput_ignore_action;
    if (!operations->reload_udev(operations->userdata))
        return INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_RELOAD_FAILED;
    if (has_source_remediation) {
        if (operations->trigger_source == NULL ||
            !operations->trigger_source(readiness->supplied_source_path,
                operations->userdata))
            return INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_TRIGGER_FAILED;
        if (operations->settle_udev == NULL ||
            !operations->settle_udev(operations->userdata))
            return INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_SETTLE_FAILED;
        if (resolution->source_permission_action &&
            (operations->verify_source_permission == NULL ||
             !operations->verify_source_permission(
                 readiness->supplied_source_path,
                 deployment, operations->userdata)))
            return INPUT_PROXY_INSTALLATION_ACTIVATION_PERMISSION_VERIFICATION_FAILED;
        if (resolution->libinput_ignore_action &&
            (operations->verify_libinput_ignore == NULL ||
             !operations->verify_libinput_ignore(
                 readiness->supplied_source_path,
                 deployment, operations->userdata)))
            return INPUT_PROXY_INSTALLATION_ACTIVATION_LIBINPUT_VERIFICATION_FAILED;
    }
    if (snprintf(unit, sizeof(unit), "input-proxy@%s.service",
            config->instance_name) >= (int)sizeof(unit))
        return INPUT_PROXY_INSTALLATION_ACTIVATION_INVALID_PLAN;
    if (!operations->enable_service(unit, operations->userdata))
        return INPUT_PROXY_INSTALLATION_ACTIVATION_ENABLE_FAILED;
    input_proxy_installation_plan_release_runtime_name(plan);
    if (!operations->start_service(unit, operations->userdata))
        return INPUT_PROXY_INSTALLATION_ACTIVATION_START_FAILED;
    state = operations->service_state(unit, operations->userdata);
    if (state == INPUT_PROXY_INSTALLATION_SERVICE_RUNNING) {
        if (!operations->source_available(config->source_path, deployment,
                operations->userdata))
            return INPUT_PROXY_INSTALLATION_ACTIVATION_SUCCESS;
        if (!operations->settle_udev(operations->userdata))
            return INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_SETTLE_FAILED;
        for (attempt = 0; attempt < VIRTUAL_OUTPUT_ATTEMPTS; ++attempt) {
            virtual_status = operations->verify_virtual_permission(
                config->instance_name, deployment, operations->userdata);
            if (virtual_status == INPUT_PROXY_VIRTUAL_OUTPUT_READABLE)
                return INPUT_PROXY_INSTALLATION_ACTIVATION_SUCCESS;
            if (virtual_status == INPUT_PROXY_VIRTUAL_OUTPUT_UNREADABLE)
                return INPUT_PROXY_INSTALLATION_ACTIVATION_VIRTUAL_PERMISSION_VERIFICATION_FAILED;
            if (!operations->source_available(config->source_path, deployment,
                    operations->userdata))
                return INPUT_PROXY_INSTALLATION_ACTIVATION_SUCCESS;
            if (attempt + 1 < VIRTUAL_OUTPUT_ATTEMPTS)
                operations->wait_virtual_output(operations->userdata);
        }
        return INPUT_PROXY_INSTALLATION_ACTIVATION_VIRTUAL_OUTPUT_NOT_FOUND;
    }
    if (state == INPUT_PROXY_INSTALLATION_SERVICE_FAILED)
        return INPUT_PROXY_INSTALLATION_ACTIVATION_SERVICE_FAILED;
    if (state == INPUT_PROXY_INSTALLATION_SERVICE_INACTIVE)
        return INPUT_PROXY_INSTALLATION_ACTIVATION_SERVICE_INACTIVE;
    return INPUT_PROXY_INSTALLATION_ACTIVATION_SERVICE_MANAGEMENT_FAILED;
}
