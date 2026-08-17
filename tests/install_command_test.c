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
        .installed_instance_directory = directory, .deployment = &deployment
    };

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
        check_command(args, 10, &environment, true, alias,
            NULL, "explicit preferred and ignore choices produce ready plan");
    }
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "RetainSource", "--use-preferred-run-source", "no",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 10, &environment, true,
            "Libinput-ignore rule: no", NULL,
            "explicit retain and ignore decline produce ready plan");
    }
    fixture.source_mode = 0600;
    {
        char *args[] = {"input-proxy", "install", "--source", source,
            "--name", "PermissionAccepted", "--use-preferred-run-source", "no",
            "--add-source-permission-rule", "yes",
            "--add-libinput-ignore-rule", "no"};
        check_command(args, 12, &environment, true,
            "Source-permission rule: would be installed", NULL,
            "required permission remediation is planned");
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
