#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "install_command_internal.h"

#include "installation_planner_internal.h"
#include "installation_application_internal.h"
#include "installation_activation_internal.h"
#include "runtime_policy_internal.h"

#include <errno.h>
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
        "Plan installation and apply one persistent input-proxy instance.\n\n"
        "Usage:\n"
        "  input-proxy install [OPTIONS]\n\n"
        "Required values may be entered interactively when omitted. This\n"
        "command installs, enables, and starts the instance.\n\n"
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

static bool prompt_line(const char *label, char **value,
    const struct input_proxy_install_command_environment *environment)
{
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;

    if (!environment->interactive) {
        fprintf(environment->error, "input-proxy: %s is required, but standard input is not interactive\n", label);
        return false;
    }
    fprintf(environment->output, "%s: ", label);
    fflush(environment->output);
    length = getline(&line, &capacity, environment->input);
    if (length < 0) {
        fprintf(environment->error, "input-proxy: failed to read %s\n", label);
        free(line);
        return false;
    }
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
        line[--length] = '\0';
    if (length == 0) {
        fprintf(environment->error, "input-proxy: %s must not be empty\n", label);
        free(line);
        return false;
    }
    *value = line;
    return true;
}

static bool prompt_yes_no(const char *prompt, bool *answer,
    const struct input_proxy_install_command_environment *environment)
{
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;

    if (!environment->interactive) {
        fprintf(environment->error, "input-proxy: an installation decision is required, but standard input is not interactive\n");
        return false;
    }
    fprintf(environment->output, "%s [Y/n]: ", prompt);
    fflush(environment->output);
    length = getline(&line, &capacity, environment->input);
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
        fprintf(environment->error, "input-proxy: expected a yes or no answer\n");
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

static enum input_proxy_install_service_identity_result service_environment(
    struct input_proxy_deployment_environment *env, gid_t **groups)
{
    struct passwd *account;
    struct group *group;
    uid_t service_uid;
    gid_t service_gid;
    int count = 0;

    errno = 0;
    account = getpwnam(SERVICE_IDENTITY);
    if (account == NULL)
        return errno == 0 ? INPUT_PROXY_INSTALL_SERVICE_USER_MISSING
                          : INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE;
    service_uid = account->pw_uid;
    service_gid = account->pw_gid;
    errno = 0;
    group = getgrnam(SERVICE_IDENTITY);
    if (group == NULL)
        return errno == 0 ? INPUT_PROXY_INSTALL_SERVICE_GROUP_MISSING
                          : INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE;
    if (service_gid != group->gr_gid)
        return INPUT_PROXY_INSTALL_SERVICE_PRIMARY_GROUP_MISMATCH;
    (void)getgrouplist(SERVICE_IDENTITY, service_gid, NULL, &count);
    if (count <= 0) count = 1;
    *groups = malloc((size_t)count * sizeof(**groups));
    if (*groups == NULL)
        return INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE;
    if (getgrouplist(SERVICE_IDENTITY, service_gid, *groups, &count) < 0) {
        free(*groups); *groups = NULL;
        return INPUT_PROXY_INSTALL_SERVICE_IDENTITY_UNUSABLE;
    }
    *env = (struct input_proxy_deployment_environment) {
        .sysfs_input_path = "/sys/class/input", .device_input_path = "/dev/input",
        .uinput_path = "/dev/uinput", .udev_data_path = "/run/udev/data",
        .service_uid = service_uid, .service_gid = service_gid,
        .service_groups = *groups, .service_group_count = (size_t)count
    };
    return INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID;
}

static void report_service_identity_error(
    enum input_proxy_install_service_identity_result result, FILE *error)
{
    if (result == INPUT_PROXY_INSTALL_SERVICE_USER_MISSING)
        fprintf(error, "input-proxy: required service user '%s' is missing; install or repair the package first\n", SERVICE_IDENTITY);
    else if (result == INPUT_PROXY_INSTALL_SERVICE_GROUP_MISSING)
        fprintf(error, "input-proxy: required service group '%s' is missing; install or repair the package first\n", SERVICE_IDENTITY);
    else if (result == INPUT_PROXY_INSTALL_SERVICE_PRIMARY_GROUP_MISMATCH)
        fprintf(error, "input-proxy: service user '%s' must have the dedicated '%s' group as its primary group; install or repair the package first\n", SERVICE_IDENTITY, SERVICE_IDENTITY);
    else
        fprintf(error, "input-proxy: required service identity '%s' is unusable; install or repair the package first\n", SERVICE_IDENTITY);
}

static void print_plan(const struct input_proxy_session_config *config,
    const struct input_proxy_deployment_readiness *readiness,
    const struct input_proxy_deployment_resolution *resolution, FILE *output)
{
    const char *source_permission_state;
    const char *libinput_ignore_state;

    if (resolution->source_permission_action) {
        source_permission_state = "yes";
    } else if (readiness->source_accessible) {
        source_permission_state = "not required";
    } else if (readiness->source_permission_remediation ==
               INPUT_PROXY_PERMISSION_REMEDIATION_AVAILABLE) {
        source_permission_state = "no";
    } else {
        source_permission_state = "unavailable";
    }

    if (resolution->libinput_ignore_action) {
        libinput_ignore_state = "yes";
    } else if (readiness->libinput_status ==
               INPUT_PROXY_LIBINPUT_STATUS_IGNORED) {
        libinput_ignore_state = "not required";
    } else if (readiness->libinput_ignore_rule_available) {
        libinput_ignore_state = "no";
    } else {
        libinput_ignore_state = "unavailable";
    }

    fprintf(output, "\nInstallation plan\n"
        "  Instance Name: %s\n  Physical Source: %s\n",
        config->instance_name,
        resolution->persistent_source_path);
    fprintf(output, "  Activity timeout: %" PRIu64 " ms\n"
        "  Detection throttle: %" PRIu64 " ms\n"
        "  Running-motion activity: %s\n  Paused-motion activity: %s\n"
        "  Initial paused policy: %s\n  Source-permission rule: %s\n"
        "  Libinput-ignore rule: %s\n  Virtual-output permission rule: required\n"
        "  /dev/uinput ready: %s\n"
        "  Application ready: %s\n",
        config->activity_timeout_ms, config->detection_throttle_ms,
        config->running_motion_activity ? "on" : "off",
        config->paused_motion_activity ? "on" : "off",
        config->start_paused ? "on" : "off",
        source_permission_state,
        libinput_ignore_state,
        readiness->uinput_accessible ? "yes" : "no",
        resolution->application_ready ? "yes" : "no");
}

static void report_plan_error(enum input_proxy_installation_plan_result result,
    const char *name, FILE *error)
{
    if (result == INPUT_PROXY_INSTALLATION_PLAN_INVALID_INSTANCE_NAME)
        fprintf(error, "input-proxy: invalid Instance Name '%s': use 1-79 ASCII bytes beginning with a letter or underscore\n", name);
    else if (result == INPUT_PROXY_INSTALLATION_PLAN_INSTALLED_NAME_COLLISION)
        fprintf(error, "input-proxy: Instance Name '%s' is already installed\n", name);
    else if (result == INPUT_PROXY_INSTALLATION_PLAN_RUNTIME_NAME_COLLISION)
        fprintf(error, "input-proxy: Instance Name '%s' is currently owned by a running input-proxy instance\n", name);
    else if (result == INPUT_PROXY_INSTALLATION_PLAN_STORE_FAILED)
        fprintf(error, "input-proxy: failed to inspect the Installed Instance store\n");
    else if (result == INPUT_PROXY_INSTALLATION_PLAN_OWNERSHIP_FAILED)
        fprintf(error, "input-proxy: failed to reserve the runtime Instance Name\n");
    else
        fprintf(error, "input-proxy: invalid installation request or planning failure\n");
}

int input_proxy_install_command_with_environment(int argc, char *argv[],
    const struct input_proxy_install_command_environment *command_environment)
{
    struct input_proxy_installation_request request;
    struct input_proxy_deployment_choices choices;
    struct input_proxy_installed_instance_store *store = NULL;
    struct input_proxy_installation_plan *plan = NULL;
    struct input_proxy_deployment_environment resolved_deployment;
    const struct input_proxy_deployment_environment *deployment;
    const struct input_proxy_deployment_readiness *readiness;
    const struct input_proxy_deployment_resolution *resolution;
    enum input_proxy_installation_plan_result result;
    enum input_proxy_installation_application_result application_result;
    struct input_proxy_installation_application_failure application_failure;
    struct input_proxy_installation_application_environment application_environment;
    struct input_proxy_installation_activation_environment activation_environment;
    enum input_proxy_installation_activation_result activation_result;
    enum input_proxy_install_service_identity_result identity_result;
    gid_t *groups = NULL;
    char *prompted_source = NULL, *prompted_name = NULL;
    bool answer;
    int exit_status = EXIT_FAILURE;

    if (command_environment == NULL || command_environment->input == NULL ||
        command_environment->output == NULL || command_environment->error == NULL) {
        return EXIT_FAILURE;
    }
    if (command_environment->effective_uid != 0) {
        fprintf(command_environment->error, "input-proxy: install requires root privileges; rerun this command with sudo\n\n");
        return EXIT_FAILURE;
    }
    if (!parse_arguments(argc, argv, &request, &choices)) {
        fprintf(command_environment->error, "input-proxy: invalid install arguments\n");
        input_proxy_install_print_help(command_environment->error);
        return EXIT_FAILURE;
    }
    if (!request.source_supplied) {
        if (!prompt_line("Physical source", &prompted_source, command_environment)) goto cleanup_with_spacing;
        request.source_path = prompted_source; request.source_supplied = true;
    }
    if (!request.instance_name_supplied) {
        if (!prompt_line("Instance Name", &prompted_name, command_environment)) goto cleanup_with_spacing;
        request.instance_name = prompted_name; request.instance_name_supplied = true;
    }
    if ((command_environment->installed_instance_directory == NULL
            ? input_proxy_installed_instance_store_create(&store)
            : input_proxy_installed_instance_store_create_for_directory(
                &store, command_environment->installed_instance_directory)) !=
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS) {
        fprintf(command_environment->error, "input-proxy: failed to initialize the Installed Instance store\n"); goto cleanup_with_spacing;
    }
    result = input_proxy_installation_plan_create(&plan, &request, store);
    if (result != INPUT_PROXY_INSTALLATION_PLAN_SUCCESS) { report_plan_error(result, request.instance_name, command_environment->error); goto cleanup_with_spacing; }
    deployment = command_environment->deployment;
    if (deployment == NULL) {
        identity_result = service_environment(&resolved_deployment, &groups);
        if (identity_result != INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID) {
            report_service_identity_error(identity_result,
                command_environment->error);
            goto cleanup_with_spacing;
        }
        deployment = &resolved_deployment;
    } else if (command_environment->check_service_identity != NULL) {
        identity_result = command_environment->check_service_identity(
            command_environment->service_identity_userdata);
        if (identity_result != INPUT_PROXY_INSTALL_SERVICE_IDENTITY_VALID) {
            report_service_identity_error(identity_result,
                command_environment->error);
            goto cleanup_with_spacing;
        }
    }
    result = input_proxy_installation_plan_assess(plan, deployment);
    if (result != INPUT_PROXY_INSTALLATION_PLAN_SUCCESS) { report_plan_error(result, request.instance_name, command_environment->error); goto cleanup_with_spacing; }
    readiness = input_proxy_installation_plan_readiness(plan);
    if (readiness->preferred_source_differs && choices.preferred_source == INPUT_PROXY_PREFERRED_SOURCE_UNRESOLVED) {
        fprintf(command_environment->output, "\nPreferred run source:\n  %s\n\nThe Preferred run source is more stable than the supplied source %s and is recommended for persistent execution.\n\n", readiness->preferred_source_path, readiness->supplied_source_path);
        if (!prompt_yes_no("Use the Preferred run source?", &answer, command_environment)) goto cleanup_with_spacing;
        choices.preferred_source = answer ? INPUT_PROXY_PREFERRED_SOURCE_USE_PREFERRED : INPUT_PROXY_PREFERRED_SOURCE_RETAIN_SUPPLIED;
    }
    if (!readiness->source_accessible && readiness->source_permission_remediation == INPUT_PROXY_PERMISSION_REMEDIATION_AVAILABLE && choices.source_permission == INPUT_PROXY_REMEDIATION_UNRESOLVED) {
        fprintf(command_environment->output, "\nThe input-proxy service identity cannot currently read this source.\nA narrowly matched instance-owned udev rule can grant access to this Physical Source.\n\n");
        if (!prompt_yes_no("Install the source-permission rule?", &answer, command_environment)) goto cleanup_with_spacing;
        choices.source_permission = answer ? INPUT_PROXY_REMEDIATION_INSTALL : INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL;
    }
    if (readiness->libinput_ignore_rule_available && choices.libinput_ignore == INPUT_PROXY_REMEDIATION_UNRESOLVED) {
        fprintf(command_environment->output, "\nThis Physical Source may also be consumed directly by libinput.\ninput-proxy can install a narrowly matched instance-owned udev rule setting LIBINPUT_IGNORE_DEVICE=1.\n\n");
        if (!prompt_yes_no("Install the libinput-ignore rule?", &answer, command_environment)) goto cleanup_with_spacing;
        choices.libinput_ignore = answer ? INPUT_PROXY_REMEDIATION_INSTALL : INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL;
    }
    result = input_proxy_installation_plan_resolve(plan, &choices);
    if (result != INPUT_PROXY_INSTALLATION_PLAN_SUCCESS) {
        fprintf(command_environment->error, "input-proxy: a deployment choice is invalid for the assessed Physical Source\n"); goto cleanup_with_spacing;
    }
    resolution = input_proxy_installation_plan_resolution(plan);
    print_plan(input_proxy_installation_plan_config(plan), readiness, resolution,
        command_environment->output);
    if (!resolution->application_ready) {
        if (!resolution->choices_resolved) fprintf(command_environment->error, "input-proxy: the installation plan has an unresolved required decision\n");
        if (!readiness->source_accessible &&
            readiness->source_permission_remediation == INPUT_PROXY_PERMISSION_REMEDIATION_AVAILABLE &&
            !resolution->source_permission_action) {
            fprintf(command_environment->error, "input-proxy: the service identity cannot read the Physical Source and required source-permission remediation was declined\n");
        }
        if (readiness->blockers & INPUT_PROXY_DEPLOYMENT_BLOCKER_SOURCE) fprintf(command_environment->error, "input-proxy: Physical Source readiness failed\n");
        if (readiness->blockers & INPUT_PROXY_DEPLOYMENT_BLOCKER_SOURCE_PERMISSION) fprintf(command_environment->error, "input-proxy: service identity cannot read the source and safe targeted remediation is unavailable\n");
        if (readiness->blockers & INPUT_PROXY_DEPLOYMENT_BLOCKER_UINPUT) fprintf(command_environment->error, "input-proxy: /dev/uinput is not ready for the service identity; install or repair package integration\n");
        goto cleanup_with_spacing;
    }
    application_environment = (struct input_proxy_installation_application_environment) {
        .udev_rule_directory = command_environment->udev_rule_directory,
        .inject_rule_publication_failure =
            command_environment->inject_rule_publication_failure,
        .inject_response_rollback_failure =
            command_environment->inject_response_rollback_failure
    };
    application_result = input_proxy_installation_plan_apply(plan, store,
        &application_environment, &application_failure);
    if (application_result != INPUT_PROXY_INSTALLATION_APPLICATION_SUCCESS) {
        if (application_result == INPUT_PROXY_INSTALLATION_APPLICATION_RESPONSE_FAILED)
            fprintf(command_environment->error, "input-proxy: failed to create the Installed Instance response artifact\n");
        else if (application_result == INPUT_PROXY_INSTALLATION_APPLICATION_RULE_GENERATION_FAILED)
            fprintf(command_environment->error, "input-proxy: failed to generate a safe instance-owned udev rule\n");
        else if (application_result == INPUT_PROXY_INSTALLATION_APPLICATION_RULE_PUBLICATION_FAILED)
            fprintf(command_environment->error, "input-proxy: failed to publish the instance-owned udev rule; newly created artifacts were rolled back\n");
        else if (application_result == INPUT_PROXY_INSTALLATION_APPLICATION_ROLLBACK_FAILED)
            fprintf(command_environment->error, "input-proxy: persistent application failed and rollback could not remove %s\n", application_failure.rollback_path);
        else
            fprintf(command_environment->error, "input-proxy: refused to apply an invalid or non-ready installation plan\n");
        goto cleanup_with_spacing;
    }
    activation_environment = (struct input_proxy_installation_activation_environment) {
        .deployment = deployment,
        .operations = command_environment->activation_operations
    };
    activation_result = input_proxy_installation_plan_activate(plan,
        &activation_environment);
    if (activation_result != INPUT_PROXY_INSTALLATION_ACTIVATION_SUCCESS) {
        fprintf(command_environment->error,
            "input-proxy: Installed Instance '%s' exists, but activation failed: ",
            request.instance_name);
        switch (activation_result) {
        case INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_RELOAD_FAILED:
            fputs("failed to reload udev rules\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_TRIGGER_FAILED:
            fputs("failed to apply the udev rule to the Physical Source\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_SETTLE_FAILED:
            fputs("udev processing did not settle\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_PERMISSION_VERIFICATION_FAILED:
            fputs("the service identity still cannot read the Physical Source\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_LIBINPUT_VERIFICATION_FAILED:
            fputs("LIBINPUT_IGNORE_DEVICE=1 was not observed\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_VIRTUAL_OUTPUT_NOT_FOUND:
            fputs("the Instance's virtual event device did not appear before the verification deadline\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_VIRTUAL_PERMISSION_VERIFICATION_FAILED:
            fputs("the service identity cannot read the Instance's virtual event device\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_ENABLE_FAILED:
            fputs("failed to enable the systemd service instance\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_START_FAILED:
            fputs("failed to start the enabled systemd service instance\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_SERVICE_FAILED:
            fputs("the enabled systemd service entered the failed state\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_SERVICE_INACTIVE:
            fputs("the enabled systemd service is not running\n", command_environment->error); break;
        case INPUT_PROXY_INSTALLATION_ACTIVATION_SERVICE_MANAGEMENT_FAILED:
            fputs("could not inspect the systemd service state\n", command_environment->error); break;
        default:
            fputs("invalid activation plan\n", command_environment->error); break;
        }
        goto cleanup_with_spacing;
    }
    fprintf(command_environment->output,
        "\nPlanning completed.\nPersistent artifacts applied.\n"
        "Installed Instance '%s' is installed, enabled, and running.\n\n",
        request.instance_name);
    exit_status = EXIT_SUCCESS;
    goto cleanup;
cleanup_with_spacing:
    fputc('\n', command_environment->error);
cleanup:
    free(groups); input_proxy_installation_plan_destroy(plan);
    input_proxy_installed_instance_store_destroy(store);
    free(prompted_source); free(prompted_name);
    return exit_status;
}

int input_proxy_install_command(int argc, char *argv[])
{
    const struct input_proxy_install_command_environment environment = {
        .effective_uid = geteuid(),
        .interactive = isatty(STDIN_FILENO),
        .input = stdin,
        .output = stdout,
        .error = stderr
    };

    return input_proxy_install_command_with_environment(argc, argv,
        &environment);
}
