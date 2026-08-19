#define _POSIX_C_SOURCE 200809L

#include "install_command_internal.h"
#include "installed_instance_internal.h"
#include "instance_name_internal.h"

#include <input_proxy/proxy_session.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
static void expect(bool condition, const char *description);

struct activation_fixture {
    char sequence[64];
    enum input_proxy_installation_activation_result failure;
    enum input_proxy_installation_service_state state;
    bool collide_on_start;
    struct input_proxy_instance_name *collision;
};

static void record_activation(struct activation_fixture *fixture, char code)
{
    size_t length = strlen(fixture->sequence);
    fixture->sequence[length] = code;
    fixture->sequence[length + 1] = '\0';
}

static bool activation_reload(void *userdata)
{
    struct activation_fixture *fixture = userdata;
    record_activation(fixture, 'R');
    return fixture->failure != INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_RELOAD_FAILED;
}

static bool activation_trigger(const char *value, void *userdata)
{
    struct activation_fixture *fixture = userdata;
    (void)value;
    record_activation(fixture, 'T');
    return fixture->failure != INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_TRIGGER_FAILED;
}

static bool activation_settle(void *userdata)
{
    struct activation_fixture *fixture = userdata;
    record_activation(fixture, 'S');
    return fixture->failure != INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_SETTLE_FAILED;
}

static bool activation_enable(const char *value, void *userdata)
{
    struct activation_fixture *fixture = userdata;
    (void)value;
    record_activation(fixture, 'E');
    return fixture->failure != INPUT_PROXY_INSTALLATION_ACTIVATION_ENABLE_FAILED;
}

static bool activation_start(const char *value, void *userdata)
{
    struct activation_fixture *fixture = userdata;
    (void)value;
    record_activation(fixture, 'A');
    if (fixture->collide_on_start) {
        expect(input_proxy_instance_name_acquire(&fixture->collision,
            "ActivationCollision") == INPUT_PROXY_SUCCESS,
            "runtime collision can acquire released reservation");
        return false;
    }
    return fixture->failure != INPUT_PROXY_INSTALLATION_ACTIVATION_START_FAILED;
}

static bool activation_verify_success(const char *value,
    const struct input_proxy_deployment_environment *deployment, void *userdata)
{
    struct activation_fixture *fixture = userdata;
    char code = strchr(fixture->sequence, 'P') == NULL ? 'P' : 'I';
    (void)value; (void)deployment;
    record_activation(fixture, code);
    return fixture->failure != (code == 'P'
        ? INPUT_PROXY_INSTALLATION_ACTIVATION_PERMISSION_VERIFICATION_FAILED
        : INPUT_PROXY_INSTALLATION_ACTIVATION_LIBINPUT_VERIFICATION_FAILED);
}

static enum input_proxy_installation_service_state activation_running(
    const char *unit, void *userdata)
{
    struct activation_fixture *fixture = userdata;
    (void)unit;
    record_activation(fixture, 'Q');
    return fixture->state;
}

static struct input_proxy_installation_activation_operations
activation_operations = {
    .reload_udev = activation_reload,
    .trigger_source = activation_trigger,
    .settle_udev = activation_settle,
    .verify_source_permission = activation_verify_success,
    .verify_libinput_ignore = activation_verify_success,
    .enable_service = activation_enable,
    .start_service = activation_start,
    .service_state = activation_running
};

struct fixture {
    const char *source;
    const char *uinput;
    mode_t source_mode;
};

static void expect(bool condition, const char *description)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static int fixture_stat(const char *path, struct stat *status, void *userdata)
{
    struct fixture *fixture = userdata;

    if (stat(path, status) != 0) return -1;
    status->st_uid = 2000;
    status->st_gid = 3000;
    if (strcmp(path, fixture->source) == 0)
        status->st_mode = (status->st_mode & ~0777) | fixture->source_mode;
    else if (strcmp(path, fixture->uinput) == 0)
        status->st_mode = (status->st_mode & ~0777) | 0660;
    return 0;
}

static bool make_directory(const char *path)
{
    return mkdir(path, 0700) == 0;
}

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    bool success = file != NULL && fputs(text, file) >= 0;

    if (file != NULL && fclose(file) != 0) success = false;
    return success;
}

static bool path_exists(const char *path)
{
    struct stat status;
    return lstat(path, &status) == 0;
}

static bool file_contains(const char *path, const char *text)
{
    char buffer[4096];
    FILE *file = fopen(path, "r");
    size_t size;
    if (file == NULL) return false;
    size = fread(buffer, 1, sizeof(buffer) - 1, file);
    buffer[size] = '\0';
    fclose(file);
    return strstr(buffer, text) != NULL;
}

static int run_command(char *argv[], int argc,
    struct input_proxy_install_command_environment *environment,
    char **output, char **error)
{
    size_t output_size = 0;
    size_t error_size = 0;
    int result;

    environment->output = open_memstream(output, &output_size);
    environment->error = open_memstream(error, &error_size);
    result = input_proxy_install_command_with_environment(argc, argv,
        environment);
    fclose(environment->output);
    fclose(environment->error);
    return result;
}

static void check_command(char *argv[], int argc,
    struct input_proxy_install_command_environment *environment,
    bool succeeds, const char *output_text, const char *error_text,
    const char *description)
{
    char *output = NULL;
    char *error = NULL;
    int result = run_command(argv, argc, environment, &output, &error);

    expect((result == EXIT_SUCCESS) == succeeds, description);
    if (output_text != NULL)
        expect(strstr(output, output_text) != NULL, description);
    if (error_text != NULL)
        expect(strstr(error, error_text) != NULL, description);
    free(output);
    free(error);
}

int main(void)
{
    char directory[] = "/tmp/input-proxy-install-command-test-XXXXXX";
    char sysfs[512], event_sysfs[512], device[512], capabilities[512];
    char identity[512], source[512], alias_dir[512], alias[512];
    char udev[512], udev_record[512], path[512];
    const char *uinput = "/dev/null";
    static const gid_t groups[] = {3000};
    struct fixture fixture;
    struct input_proxy_deployment_environment deployment;
    struct input_proxy_install_command_environment environment;
    struct input_proxy_installed_instance_store *store = NULL;
    struct input_proxy_instance_name *ownership = NULL;
    struct input_proxy_session_config installed = {0};
    FILE *empty_input = tmpfile();
    struct activation_fixture activation = {
        .state = INPUT_PROXY_INSTALLATION_SERVICE_RUNNING
    };

    expect(mkdtemp(directory) != NULL, "create fixture directory");
    snprintf(sysfs, sizeof(sysfs), "%s/sys", directory);
    snprintf(event_sysfs, sizeof(event_sysfs), "%s/event7", sysfs);
    snprintf(device, sizeof(device), "%s/device", event_sysfs);
    snprintf(capabilities, sizeof(capabilities), "%s/capabilities", device);
    snprintf(identity, sizeof(identity), "%s/id", device);
    snprintf(source, sizeof(source), "%s/event7", directory);
    snprintf(alias_dir, sizeof(alias_dir), "%s/by-id", directory);
    snprintf(alias, sizeof(alias), "%s/test-device", alias_dir);
    snprintf(udev, sizeof(udev), "%s/udev", directory);
    snprintf(udev_record, sizeof(udev_record), "%s/c1:3", udev);
    expect(make_directory(sysfs) && make_directory(event_sysfs) &&
        make_directory(device) && make_directory(capabilities) &&
        make_directory(identity) && make_directory(alias_dir) &&
        make_directory(udev), "create device fixture directories");
    snprintf(path, sizeof(path), "%s/name", device);
    expect(write_text(path, "Fixture Input\n"), "write device name");
    snprintf(path, sizeof(path), "%s/id/bustype", device);
    expect(write_text(path, "0003\n"), "write bus identity");
    snprintf(path, sizeof(path), "%s/capabilities/key", device);
    expect(write_text(path, "1\n"), "write key capabilities");
    snprintf(path, sizeof(path), "%s/capabilities/abs", device);
    expect(write_text(path, "0\n"), "write abs capabilities");
    snprintf(path, sizeof(path), "%s/capabilities/rel", device);
    expect(write_text(path, "0\n"), "write rel capabilities");
    snprintf(path, sizeof(path), "%s/properties", device);
    expect(write_text(path, "0\n"), "write properties");
    snprintf(path, sizeof(path), "%s/dev", event_sysfs);
    expect(write_text(path, "1:3\n"), "write device number");
    expect(symlink("/dev/null", source) == 0 &&
        symlink("../event7", alias) == 0, "create source aliases");
    expect(write_text(udev_record,
        "E:ID_VENDOR_ID=1234\nE:ID_MODEL_ID=5678\nE:ID_PATH=platform-test\n"),
        "write narrow udev identity");

    fixture = (struct fixture) {
        .source = source, .uinput = uinput, .source_mode = 0640
    };
    deployment = (struct input_proxy_deployment_environment) {
        .sysfs_input_path = sysfs, .device_input_path = directory,
        .uinput_path = uinput, .udev_data_path = udev,
        .service_uid = 1000, .service_gid = 1000,
        .service_groups = groups, .service_group_count = 1,
        .stat_path = fixture_stat, .stat_userdata = &fixture
    };
    environment = (struct input_proxy_install_command_environment) {
        .effective_uid = 0, .interactive = false, .input = empty_input,
        .installed_instance_directory = directory,
        .udev_rule_directory = directory, .deployment = &deployment,
        .activation_operations = &activation_operations
    };
    activation_operations.userdata = &activation;

    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "Unprivileged"};
        environment.effective_uid = 1000;
        check_command(args, 6, &environment, false, NULL, "must be run as root",
            "unprivileged invocation fails before planning");
        environment.effective_uid = 0;
    }
    {
        char *args[] = {"input-proxy", "install", "--name", "MissingSource"};
        check_command(args, 4, &environment, false, NULL,
            "standard input is not interactive",
            "missing required value fails without interaction");
    }
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "ExplicitPreferred", "--use-preferred-run-source", "yes",
            "--add-libinput-ignore-rule", "yes"};
        char expected[640];
        snprintf(expected, sizeof(expected), "Physical Source: %s", alias);
        check_command(args, 10, &environment, true, expected,
            NULL, "explicit preferred and ignore choices produce ready plan");
        snprintf(path, sizeof(path), "%s/ExplicitPreferred.args", directory);
        expect(file_contains(path, alias), "response artifact uses resolved source");
        snprintf(path, sizeof(path), "%s/90-input-proxy-ExplicitPreferred.rules", directory);
        expect(file_contains(path, "ENV{LIBINPUT_IGNORE_DEVICE}=\"1\"") &&
            file_contains(path, "ID_VENDOR_ID"), "ignore rule uses narrow udev identity");
    }
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "RetainSource", "--use-preferred-run-source", "no",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 10, &environment, true,
            "Libinput-ignore rule: no", NULL,
            "explicit retain and ignore decline produce ready plan");
        snprintf(path, sizeof(path), "%s/RetainSource.args", directory);
        expect(path_exists(path), "no-action plan creates response artifact");
        snprintf(path, sizeof(path), "%s/90-input-proxy-RetainSource.rules", directory);
        expect(!path_exists(path), "no-action plan creates no udev rule");
    }
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "RetainPresentation", "--use-preferred-run-source", "no",
            "--add-libinput-ignore-rule", "no"};
        char expected[640];
        snprintf(expected, sizeof(expected), "Physical Source: %s", source);
        check_command(args, 10, &environment, true, expected, NULL,
            "retained supplied source is the resolved Physical Source");
    }
    fixture.source_mode = 0600;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "PermissionAccepted", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "yes",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 12, &environment, true,
            "Source-permission rule: yes", NULL,
            "required permission remediation is planned");
        snprintf(path, sizeof(path), "%s/90-input-proxy-PermissionAccepted.rules", directory);
        expect(file_contains(path, "GROUP=\"input-proxy\"") &&
            file_contains(path, "MODE=\"0640\""),
            "permission plan creates service access rule");
    }
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "BothActions", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "yes",
            "--add-libinput-ignore-rule", "yes"};
        check_command(args, 12, &environment, true,
            "Persistent artifacts applied", NULL,
            "both remediation actions apply successfully");
        snprintf(path, sizeof(path), "%s/90-input-proxy-BothActions.rules", directory);
        expect(file_contains(path, "GROUP=\"input-proxy\"") &&
            file_contains(path, "LIBINPUT_IGNORE_DEVICE"),
            "both actions share one udev rule");
    }
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "PublishFailure", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "yes",
            "--add-libinput-ignore-rule", "no"};
        environment.inject_rule_publication_failure = true;
        check_command(args, 12, &environment, false, NULL, "rolled back",
            "udev publication failure is reported");
        environment.inject_rule_publication_failure = false;
        snprintf(path, sizeof(path), "%s/PublishFailure.args", directory);
        expect(!path_exists(path), "publication failure rolls back response artifact");
    }
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "RollbackFailure", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "yes",
            "--add-libinput-ignore-rule", "no"};
        environment.inject_rule_publication_failure = true;
        environment.inject_response_rollback_failure = true;
        check_command(args, 12, &environment, false, NULL,
            "rollback could not remove", "rollback failure identifies partial state");
        environment.inject_rule_publication_failure = false;
        environment.inject_response_rollback_failure = false;
    }
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "PermissionDeclined", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "no",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 12, &environment, false,
            "Application ready: no", "required source-permission remediation was declined",
            "declined required remediation explains non-ready plan");
    }
    fixture.source_mode = 0640;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "PermissionNotRequired", "--use-preferred-run-source", "no",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 10, &environment, true,
            "Source-permission rule: not required", NULL,
            "existing source access reports permission rule not required");
    }
    expect(write_text(udev_record,
        "E:ID_VENDOR_ID=1234\nE:ID_MODEL_ID=5678\nE:ID_PATH=platform-test\n"
        "E:LIBINPUT_IGNORE_DEVICE=1\n"), "mark source ignored by libinput");
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "IgnoreNotRequired", "--use-preferred-run-source", "no"};
        check_command(args, 8, &environment, true,
            "Libinput-ignore rule: not required", NULL,
            "existing libinput ignore reports rule not required");
    }
    expect(write_text(udev_record, "E:ID_VENDOR_ID=1234\n"),
        "remove narrow remediation identity");
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "IgnoreUnavailable", "--use-preferred-run-source", "no"};
        check_command(args, 8, &environment, true,
            "Libinput-ignore rule: unavailable", NULL,
            "unsafe libinput remediation is reported as unavailable");
    }
    snprintf(path, sizeof(path), "%s/id/vendor", device);
    expect(write_text(path, "27c6\n"), "write kernel vendor identity");
    snprintf(path, sizeof(path), "%s/id/product", device);
    expect(write_text(path, "0113\n"), "write kernel product identity");
    expect(write_text(udev_record, "E:ID_PATH=platform-test-i2c\n"),
        "write Goodix-style path identity");
    fixture.source_mode = 0600;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "GoodixStyle", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "yes",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 12, &environment, true,
            "Persistent artifacts applied", NULL,
            "kernel input identity can apply a permission rule");
        snprintf(path, sizeof(path), "%s/90-input-proxy-GoodixStyle.rules", directory);
        expect(file_contains(path, "ATTRS{id/bustype}==\"0003\"") &&
            file_contains(path, "ATTRS{id/vendor}==\"27c6\"") &&
            file_contains(path, "ATTRS{id/product}==\"0113\""),
            "Goodix-style rule uses kernel identity plus path");
    }
    expect(write_text(udev_record,
        "E:ID_VENDOR_ID=1234\nE:ID_MODEL_ID=5678\nE:ID_PATH=platform-test\n"),
        "restore narrow remediation identity");
    {
        char rule[512];
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "ExistingRule", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "yes",
            "--add-libinput-ignore-rule", "no"};
        snprintf(rule, sizeof(rule), "%s/90-input-proxy-ExistingRule.rules", directory);
        expect(write_text(rule, "pre-existing\n"), "create pre-existing udev rule");
        check_command(args, 12, &environment, false, NULL,
            "failed to publish", "existing udev rule prevents application");
        expect(file_contains(rule, "pre-existing"), "existing udev rule is preserved");
        snprintf(path, sizeof(path), "%s/ExistingRule.args", directory);
        expect(!path_exists(path), "existing udev rule leaves no response artifact");
    }
    fixture.source_mode = 0640;
    expect(input_proxy_installed_instance_store_create_for_directory(
        &store, directory) == INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS,
        "open fixture Installed Instance store");
    installed = (struct input_proxy_session_config) {
        .source_path = source, .instance_name = "InstalledCollision",
        .activity_timeout_ms = 5000, .detection_throttle_ms = 250,
        .running_motion_activity = true, .paused_motion_activity = true
    };
    expect(input_proxy_installed_instance_create(store, &installed) ==
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS, "create installed collision");

    memset(&activation, 0, sizeof(activation));
    activation.state = INPUT_PROXY_INSTALLATION_SERVICE_RUNNING;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "ActivationNoRule", "--use-preferred-run-source", "no",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 10, &environment, true, "installed, enabled, and running",
            NULL, "activation without a udev rule succeeds");
        expect(strcmp(activation.sequence, "EAQ") == 0,
            "activation without a rule skips all udev operations");
    }
    memset(&activation, 0, sizeof(activation));
    activation.state = INPUT_PROXY_INSTALLATION_SERVICE_RUNNING;
    fixture.source_mode = 0600;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "ActivationWithRule", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "yes",
            "--add-libinput-ignore-rule", "yes"};
        check_command(args, 12, &environment, true, "installed, enabled, and running",
            NULL, "activation with a udev rule succeeds");
        expect(strcmp(activation.sequence, "RTSPIEAQ") == 0,
            "udev activation and verification precede systemd activation");
    }
    memset(&activation, 0, sizeof(activation));
    activation.state = INPUT_PROXY_INSTALLATION_SERVICE_RUNNING;
    activation.failure = INPUT_PROXY_INSTALLATION_ACTIVATION_UDEV_RELOAD_FAILED;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "ActivationReloadFailure", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "yes",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 12, &environment, false, NULL,
            "exists, but activation failed: failed to reload udev rules",
            "udev reload failure is a distinct post-commit failure");
        snprintf(path, sizeof(path), "%s/ActivationReloadFailure.args", directory);
        expect(path_exists(path) && strcmp(activation.sequence, "R") == 0,
            "udev reload failure preserves installation and skips systemd");
    }
    memset(&activation, 0, sizeof(activation));
    activation.state = INPUT_PROXY_INSTALLATION_SERVICE_RUNNING;
    activation.failure = INPUT_PROXY_INSTALLATION_ACTIVATION_PERMISSION_VERIFICATION_FAILED;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "ActivationVerifyFailure", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "yes",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 12, &environment, false, NULL,
            "service identity still cannot read",
            "udev remediation verification failure is reported");
        snprintf(path, sizeof(path), "%s/ActivationVerifyFailure.args", directory);
        expect(path_exists(path) && strcmp(activation.sequence, "RTSP") == 0,
            "verification failure preserves installation and skips systemd");
    }
    fixture.source_mode = 0640;
    memset(&activation, 0, sizeof(activation));
    activation.state = INPUT_PROXY_INSTALLATION_SERVICE_RUNNING;
    activation.failure = INPUT_PROXY_INSTALLATION_ACTIVATION_ENABLE_FAILED;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "ActivationEnableFailure", "--use-preferred-run-source", "no",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 10, &environment, false, NULL, "failed to enable",
            "systemd enable failure is reported after commit");
        snprintf(path, sizeof(path), "%s/ActivationEnableFailure.args", directory);
        expect(path_exists(path) && strcmp(activation.sequence, "E") == 0,
            "enable failure preserves installation and does not start");
    }
    memset(&activation, 0, sizeof(activation));
    activation.state = INPUT_PROXY_INSTALLATION_SERVICE_RUNNING;
    activation.failure = INPUT_PROXY_INSTALLATION_ACTIVATION_START_FAILED;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "ActivationStartFailure", "--use-preferred-run-source", "no",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 10, &environment, false, NULL, "failed to start the enabled",
            "systemd start failure is reported after enablement");
        expect(strcmp(activation.sequence, "EA") == 0,
            "start failure leaves prior enablement intact");
    }
    memset(&activation, 0, sizeof(activation));
    activation.state = INPUT_PROXY_INSTALLATION_SERVICE_FAILED;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "ActivationServiceFailed", "--use-preferred-run-source", "no",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 10, &environment, false, NULL, "entered the failed state",
            "immediate service failure is not reported as success");
        expect(strcmp(activation.sequence, "EAQ") == 0,
            "service state is inspected after successful start invocation");
    }
    memset(&activation, 0, sizeof(activation));
    activation.state = INPUT_PROXY_INSTALLATION_SERVICE_RUNNING;
    activation.collide_on_start = true;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "ActivationCollision", "--use-preferred-run-source", "no",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 10, &environment, false, NULL, "failed to start the enabled",
            "runtime collision after release follows ordinary start failure path");
        snprintf(path, sizeof(path), "%s/ActivationCollision.args", directory);
        expect(path_exists(path) && activation.collision != NULL,
            "runtime collision preserves Installed Instance artifacts");
        input_proxy_instance_name_release(activation.collision);
        activation.collision = NULL;
    }
    memset(&activation, 0, sizeof(activation));
    activation.state = INPUT_PROXY_INSTALLATION_SERVICE_RUNNING;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "InstalledCollision"};
        check_command(args, 6, &environment, false, NULL, "already installed",
            "Installed Instance collision is distinct");
    }
    expect(input_proxy_instance_name_acquire(&ownership, "RunningCollision") ==
        INPUT_PROXY_SUCCESS, "reserve running collision");
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "RunningCollision"};
        check_command(args, 6, &environment, false, NULL,
            "currently owned by a running", "running collision is distinct");
    }

    input_proxy_instance_name_release(ownership);
    input_proxy_installed_instance_store_destroy(store);
    fclose(empty_input);
    if (failures != 0) {
        fprintf(stderr, "%d install command test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
