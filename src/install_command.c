#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "install_command_internal.h"

#include "installation_planner_internal.h"
#include "runtime_policy_internal.h"

#include <grp.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SERVICE_IDENTITY "input-proxy"

void input_proxy_install_print_help(FILE *stream)
{
    fputs(
        "Plan installation of one persistent input-proxy instance.\n\n"
        "Usage:\n"
        "  input-proxy install [OPTIONS]\n\n"
        "Required values may be entered interactively when omitted. This\n"
        "command only produces a plan; it makes no persistent changes.\n\n"
        "Runtime policy options:\n"
        "  --source PATH\n"
        "  --name INSTANCE_NAME\n"
        "  --activity-timeout-ms MS       (default: 5000)\n"
        "  --detection-throttle-ms MS     (default: 250)\n"
        "  --running-motion-activity on|off (default: on)\n"
        "  --paused-motion-activity on|off  (default: on)\n"
        "  --start-paused on|off            (default: off)\n\n"
        "Deployment choices:\n"
        "  --use-preferred-run-source yes|no\n"
        "  --add-source-permission-rule yes|no\n"
        "  --add-libinput-ignore-rule yes|no\n"
        "  --help  Show this help and exit.\n\n",
        stream);
}

static bool parse_yes_no(const char *text, bool *answer)
{
    if (strcmp(text, "yes") == 0) { *answer = true; return true; }
    if (strcmp(text, "no") == 0) { *answer = false; return true; }
    return false;
}

static bool prompt_line(const char *label, char **value)
{
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "input-proxy: %s is required, but standard input is not interactive\n", label);
        return false;
    }
    printf("%s: ", label);
    fflush(stdout);
    length = getline(&line, &capacity, stdin);
    if (length < 0) {
        fprintf(stderr, "input-proxy: failed to read %s\n", label);
        free(line);
        return false;
    }
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        line[--length] = '\0';
    if (length == 0) {
        fprintf(stderr, "input-proxy: %s must not be empty\n", label);
        free(line);
        return false;
    }
    *value = line;
    return true;
}

static bool prompt_yes_no(const char *prompt, bool *answer)
{
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "input-proxy: an installation decision is required, but standard input is not interactive\n");
        return false;
    }
    printf("%s [Y/n]: ", prompt);
    fflush(stdout);
    length = getline(&line, &capacity, stdin);
    if (length < 0) { free(line); return false; }
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        line[--length] = '\0';
    if (length == 0 || strcmp(line, "y") == 0 || strcmp(line, "Y") == 0 ||
        strcmp(line, "yes") == 0 || strcmp(line, "YES") == 0) {
        *answer = true;
    } else if (strcmp(line, "n") == 0 || strcmp(line, "N") == 0 ||
               strcmp(line, "no") == 0 || strcmp(line, "NO") == 0) {
        *answer = false;
    } else {
        fprintf(stderr, "input-proxy: expected a yes or no answer\n");
        free(line);
        return false;
    }
    free(line);
    return true;
}

static bool parse_arguments(int argc, char *argv[],
    struct input_proxy_installation_request *request,
    struct input_proxy_deployment_choices *choices)
{
    bool seen[8] = {0};
    int index;

    input_proxy_installation_request_init(request);
    memset(choices, 0, sizeof(*choices));
    for (index = 2; index < argc; ++index) {
        const char *option = argv[index];
        const char *value;
        int field = -1;
        bool enabled;

        if (index + 1 >= argc) return false;
        value = argv[++index];
        if (strcmp(option, "--source") == 0) {
            field = 0; request->source_path = value; request->source_supplied = true;
        } else if (strcmp(option, "--name") == 0) {
            field = 1; request->instance_name = value; request->instance_name_supplied = true;
        } else if (strcmp(option, "--activity-timeout-ms") == 0) {
            field = 2; if (!input_proxy_runtime_policy_parse_duration(value, &request->activity_timeout_ms)) return false;
            request->activity_timeout_supplied = true;
        } else if (strcmp(option, "--detection-throttle-ms") == 0) {
            field = 3; if (!input_proxy_runtime_policy_parse_duration(value, &request->detection_throttle_ms)) return false;
            request->detection_throttle_supplied = true;
        } else if (strcmp(option, "--running-motion-activity") == 0) {
            field = 4; if (!input_proxy_runtime_policy_parse_on_off(value, &request->running_motion_activity)) return false;
            request->running_motion_supplied = true;
        } else if (strcmp(option, "--paused-motion-activity") == 0) {
            field = 5; if (!input_proxy_runtime_policy_parse_on_off(value, &request->paused_motion_activity)) return false;
            request->paused_motion_supplied = true;
        } else if (strcmp(option, "--start-paused") == 0) {
            field = 6; if (!input_proxy_runtime_policy_parse_on_off(value, &request->start_paused)) return false;
            request->start_paused_supplied = true;
        } else if (strcmp(option, "--use-preferred-run-source") == 0) {
            field = 7; if (!parse_yes_no(value, &enabled)) return false;
            choices->preferred_source = enabled ? INPUT_PROXY_PREFERRED_SOURCE_USE_PREFERRED : INPUT_PROXY_PREFERRED_SOURCE_RETAIN_SUPPLIED;
        } else if (strcmp(option, "--add-source-permission-rule") == 0) {
            if (choices->source_permission != INPUT_PROXY_REMEDIATION_UNRESOLVED || !parse_yes_no(value, &enabled)) return false;
            choices->source_permission = enabled ? INPUT_PROXY_REMEDIATION_INSTALL : INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL;
            continue;
        } else if (strcmp(option, "--add-libinput-ignore-rule") == 0) {
            if (choices->libinput_ignore != INPUT_PROXY_REMEDIATION_UNRESOLVED || !parse_yes_no(value, &enabled)) return false;
            choices->libinput_ignore = enabled ? INPUT_PROXY_REMEDIATION_INSTALL : INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL;
            continue;
        } else return false;
        if (seen[field]) return false;
        seen[field] = true;
    }
    return true;
}

static bool service_environment(struct input_proxy_deployment_environment *env,
    gid_t **groups)
{
    struct passwd *account = getpwnam(SERVICE_IDENTITY);
    int count = 0;

    if (account == NULL) {
        fprintf(stderr, "input-proxy: required service identity '%s' is missing; install or repair the package first\n", SERVICE_IDENTITY);
        return false;
    }
    (void)getgrouplist(account->pw_name, account->pw_gid, NULL, &count);
    if (count <= 0) count = 1;
    *groups = malloc((size_t)count * sizeof(**groups));
    if (*groups == NULL) return false;
    if (getgrouplist(account->pw_name, account->pw_gid, *groups, &count) < 0) {
        free(*groups); *groups = NULL;
        fprintf(stderr, "input-proxy: failed to resolve groups for service identity '%s'\n", SERVICE_IDENTITY);
        return false;
    }
    *env = (struct input_proxy_deployment_environment) {
        .sysfs_input_path = "/sys/class/input", .device_input_path = "/dev/input",
        .uinput_path = "/dev/uinput", .udev_data_path = "/run/udev/data",
        .service_uid = account->pw_uid, .service_gid = account->pw_gid,
        .service_groups = *groups, .service_group_count = (size_t)count
    };
    return true;
}

static void print_plan(const struct input_proxy_session_config *config,
    const struct input_proxy_deployment_readiness *readiness,
    const struct input_proxy_deployment_resolution *resolution)
{
    printf("\nInstallation plan (no changes applied)\n"
        "  Instance Name: %s\n  Supplied Physical Source: %s\n"
        "  Selected persistent source: %s\n",
        config->instance_name, readiness->supplied_source_path,
        resolution->persistent_source_path);
    if (readiness->preferred_source_path != NULL)
        printf("  Preferred run source: %s\n", readiness->preferred_source_path);
    printf("  Activity timeout: %" PRIu64 " ms\n"
        "  Detection throttle: %" PRIu64 " ms\n"
        "  Running-motion activity: %s\n  Paused-motion activity: %s\n"
        "  Initial paused policy: %s\n  Source-permission rule: %s\n"
        "  Libinput-ignore rule: %s\n  /dev/uinput ready: %s\n"
        "  Application ready: %s\n",
        config->activity_timeout_ms, config->detection_throttle_ms,
        config->running_motion_activity ? "on" : "off",
        config->paused_motion_activity ? "on" : "off",
        config->start_paused ? "on" : "off",
        resolution->source_permission_action ? "would be installed" : "no",
        resolution->libinput_ignore_action ? "would be installed" : "no",
        readiness->uinput_accessible ? "yes" : "no",
        resolution->application_ready ? "yes" : "no");
}

static void report_plan_error(enum input_proxy_installation_plan_result result,
    const char *name)
{
    if (result == INPUT_PROXY_INSTALLATION_PLAN_INVALID_INSTANCE_NAME)
        fprintf(stderr, "input-proxy: invalid Instance Name '%s': use 1-79 ASCII bytes beginning with a letter or underscore\n", name);
    else if (result == INPUT_PROXY_INSTALLATION_PLAN_INSTALLED_NAME_COLLISION)
        fprintf(stderr, "input-proxy: Instance Name '%s' is already installed\n", name);
    else if (result == INPUT_PROXY_INSTALLATION_PLAN_RUNTIME_NAME_COLLISION)
        fprintf(stderr, "input-proxy: Instance Name '%s' is currently owned by a running input-proxy instance\n", name);
    else if (result == INPUT_PROXY_INSTALLATION_PLAN_STORE_FAILED)
        fprintf(stderr, "input-proxy: failed to inspect the Installed Instance store\n");
    else if (result == INPUT_PROXY_INSTALLATION_PLAN_OWNERSHIP_FAILED)
        fprintf(stderr, "input-proxy: failed to reserve the runtime Instance Name\n");
    else
        fprintf(stderr, "input-proxy: invalid installation request or planning failure\n");
}

int input_proxy_install_command(int argc, char *argv[])
{
    struct input_proxy_installation_request request;
    struct input_proxy_deployment_choices choices;
    struct input_proxy_installed_instance_store *store = NULL;
    struct input_proxy_installation_plan *plan = NULL;
    struct input_proxy_deployment_environment environment;
    const struct input_proxy_deployment_readiness *readiness;
    const struct input_proxy_deployment_resolution *resolution;
    enum input_proxy_installation_plan_result result;
    gid_t *groups = NULL;
    char *prompted_source = NULL, *prompted_name = NULL;
    bool answer;
    int exit_status = EXIT_FAILURE;

    if (geteuid() != 0) {
        fprintf(stderr, "input-proxy: install must be run as root; normally use sudo input-proxy install ...\n\n");
        return EXIT_FAILURE;
    }
    if (!parse_arguments(argc, argv, &request, &choices)) {
        fprintf(stderr, "input-proxy: invalid install arguments\n");
        input_proxy_install_print_help(stderr);
        return EXIT_FAILURE;
    }
    if (!request.source_supplied) {
        if (!prompt_line("Physical source", &prompted_source)) goto cleanup;
        request.source_path = prompted_source; request.source_supplied = true;
    }
    if (!request.instance_name_supplied) {
        if (!prompt_line("Instance Name", &prompted_name)) goto cleanup;
        request.instance_name = prompted_name; request.instance_name_supplied = true;
    }
    if (input_proxy_installed_instance_store_create(&store) != INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
        fprintf(stderr, "input-proxy: failed to initialize the Installed Instance store\n"); goto cleanup;
    }
    result = input_proxy_installation_plan_create(&plan, &request, store);
    if (result != INPUT_PROXY_INSTALLATION_PLAN_SUCCESS) { report_plan_error(result, request.instance_name); goto cleanup; }
    if (!service_environment(&environment, &groups)) goto cleanup;
    result = input_proxy_installation_plan_assess(plan, &environment);
    if (result != INPUT_PROXY_INSTALLATION_PLAN_SUCCESS) { report_plan_error(result, request.instance_name); goto cleanup; }
    readiness = input_proxy_installation_plan_readiness(plan);
    if (readiness->preferred_source_differs && choices.preferred_source == INPUT_PROXY_PREFERRED_SOURCE_UNRESOLVED) {
        printf("\nPreferred run source:\n  %s\n\nThe Preferred run source is more stable than the supplied source %s and is recommended for persistent execution.\n\n", readiness->preferred_source_path, readiness->supplied_source_path);
        if (!prompt_yes_no("Use the Preferred run source?", &answer)) goto cleanup;
        choices.preferred_source = answer ? INPUT_PROXY_PREFERRED_SOURCE_USE_PREFERRED : INPUT_PROXY_PREFERRED_SOURCE_RETAIN_SUPPLIED;
    }
    if (!readiness->source_accessible && readiness->source_permission_remediation == INPUT_PROXY_PERMISSION_REMEDIATION_AVAILABLE && choices.source_permission == INPUT_PROXY_REMEDIATION_UNRESOLVED) {
        printf("\nThe input-proxy service identity cannot currently read this source.\nA narrowly matched instance-owned udev rule can grant access to this Physical Source.\n\n");
        if (!prompt_yes_no("Install the source-permission rule?", &answer)) goto cleanup;
        choices.source_permission = answer ? INPUT_PROXY_REMEDIATION_INSTALL : INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL;
    }
    if (readiness->libinput_ignore_rule_available && choices.libinput_ignore == INPUT_PROXY_REMEDIATION_UNRESOLVED) {
        printf("\nThis Physical Source may also be consumed directly by libinput.\ninput-proxy can install a narrowly matched instance-owned udev rule setting LIBINPUT_IGNORE_DEVICE=1.\n\n");
        if (!prompt_yes_no("Install the libinput-ignore rule?", &answer)) goto cleanup;
        choices.libinput_ignore = answer ? INPUT_PROXY_REMEDIATION_INSTALL : INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL;
    }
    result = input_proxy_installation_plan_resolve(plan, &choices);
    if (result != INPUT_PROXY_INSTALLATION_PLAN_SUCCESS) {
        fprintf(stderr, "input-proxy: a deployment choice is invalid for the assessed Physical Source\n"); goto cleanup;
    }
    resolution = input_proxy_installation_plan_resolution(plan);
    print_plan(input_proxy_installation_plan_config(plan), readiness, resolution);
    if (!resolution->application_ready) {
        if (!resolution->choices_resolved) fprintf(stderr, "input-proxy: the installation plan has an unresolved required decision\n");
        if (readiness->blockers & INPUT_PROXY_DEPLOYMENT_BLOCKER_SOURCE) fprintf(stderr, "input-proxy: Physical Source readiness failed\n");
        if (readiness->blockers & INPUT_PROXY_DEPLOYMENT_BLOCKER_SOURCE_PERMISSION) fprintf(stderr, "input-proxy: service identity cannot read the source and safe targeted remediation is unavailable\n");
        if (readiness->blockers & INPUT_PROXY_DEPLOYMENT_BLOCKER_UINPUT) fprintf(stderr, "input-proxy: /dev/uinput is not ready for the service identity; install or repair package integration\n");
        goto cleanup;
    }
    printf("\nThe plan is application-ready. No persistent changes have been applied.\n\n");
    exit_status = EXIT_SUCCESS;
cleanup:
    free(groups); input_proxy_installation_plan_destroy(plan);
    input_proxy_installed_instance_store_destroy(store);
    free(prompted_source); free(prompted_name);
    return exit_status;
}
