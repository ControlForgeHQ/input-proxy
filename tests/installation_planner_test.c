#define _POSIX_C_SOURCE 200809L

#include "installation_planner_internal.h"
#include "instance_name_internal.h"
#include "runtime_policy_internal.h"

#include <input_proxy/proxy_session.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

struct stat_fixture {
    const char *source_path;
    const char *uinput_path;
    mode_t source_mode;
    mode_t uinput_mode;
    uid_t owner;
    gid_t group;
};

static int fixture_stat(const char *path, struct stat *status, void *userdata)
{
    struct stat_fixture *fixture = userdata;
    int result = stat(path, status);

    if (result != 0) return result;
    if (strcmp(path, fixture->source_path) == 0) {
        status->st_mode = (status->st_mode & ~0777) | fixture->source_mode;
        status->st_uid = fixture->owner;
        status->st_gid = fixture->group;
    } else if (strcmp(path, fixture->uinput_path) == 0) {
        status->st_mode = (status->st_mode & ~0777) | fixture->uinput_mode;
        status->st_uid = fixture->owner;
        status->st_gid = fixture->group;
    }
    return 0;
}

static int make_directory(const char *path)
{
    return mkdir(path, 0700) == 0 ? 0 : 1;
}

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file == NULL) return 1;
    if (fputs(text, file) < 0 || fclose(file) != 0) return 1;
    return 0;
}

static void expect(bool condition, const char *description)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", description);
        failures++;
    }
}

static struct input_proxy_installation_request make_request(const char *name)
{
    struct input_proxy_installation_request request;

    input_proxy_installation_request_init(&request);
    request.source_path = "/dev/input/by-id/example";
    request.instance_name = name;
    request.source_supplied = true;
    request.instance_name_supplied = true;
    return request;
}

int main(void)
{
    char directory[] = "/tmp/input-proxy-plan-test-XXXXXX";
    struct input_proxy_installed_instance_store *store = NULL;
    struct input_proxy_installation_plan *plan = NULL;
    struct input_proxy_instance_name *ownership = NULL;
    struct input_proxy_installation_request request;
    const struct input_proxy_session_config *config;
    enum input_proxy_installation_plan_result result;
    uint64_t duration;
    bool enabled;
    char sysfs[512];
    char event_sysfs[512];
    char device_dir[512];
    char path[512];
    char source[512];
    char alias_dir[512];
    char alias[512];
    char udev[512];
    char udev_record[512];
    const char *uinput = "/dev/null";
    struct stat_fixture fixture;
    struct input_proxy_deployment_environment environment;
    const struct input_proxy_deployment_readiness *readiness;
    const struct input_proxy_deployment_resolution *resolution;
    struct input_proxy_deployment_choices choices;

    expect(mkdtemp(directory) != NULL, "create isolated artifact directory");
    expect(input_proxy_installed_instance_store_create_for_directory(
        &store,
        directory
    ) == INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS, "create isolated store");

    snprintf(sysfs, sizeof(sysfs), "%s/sys", directory);
    snprintf(event_sysfs, sizeof(event_sysfs), "%s/event7", sysfs);
    snprintf(device_dir, sizeof(device_dir), "%s/device", event_sysfs);
    snprintf(source, sizeof(source), "%s/event7", directory);
    snprintf(alias_dir, sizeof(alias_dir), "%s/by-id", directory);
    snprintf(alias, sizeof(alias), "%s/test-device", alias_dir);
    snprintf(udev, sizeof(udev), "%s/udev", directory);
    snprintf(udev_record, sizeof(udev_record), "%s/c1:3", udev);
    expect(make_directory(sysfs) == 0 && make_directory(event_sysfs) == 0 &&
        make_directory(device_dir) == 0 && make_directory(alias_dir) == 0 &&
        make_directory(udev) == 0, "create readiness fixture directories");
    snprintf(path, sizeof(path), "%s/capabilities", device_dir);
    expect(make_directory(path) == 0, "create fixture capabilities");
    snprintf(path, sizeof(path), "%s/id", device_dir);
    expect(make_directory(path) == 0, "create fixture identity directory");
    snprintf(path, sizeof(path), "%s/name", device_dir);
    expect(write_text(path, "Fixture Input\n") == 0, "write fixture name");
    snprintf(path, sizeof(path), "%s/id/bustype", device_dir);
    expect(write_text(path, "0003\n") == 0, "write fixture bus");
    snprintf(path, sizeof(path), "%s/capabilities/key", device_dir);
    expect(write_text(path, "1\n") == 0, "write fixture keys");
    snprintf(path, sizeof(path), "%s/capabilities/abs", device_dir);
    expect(write_text(path, "0\n") == 0, "write fixture abs");
    snprintf(path, sizeof(path), "%s/capabilities/rel", device_dir);
    expect(write_text(path, "0\n") == 0, "write fixture rel");
    snprintf(path, sizeof(path), "%s/properties", device_dir);
    expect(write_text(path, "0\n") == 0, "write fixture properties");
    snprintf(path, sizeof(path), "%s/dev", event_sysfs);
    expect(write_text(path, "1:3\n") == 0, "write fixture device number");
    expect(symlink("/dev/null", source) == 0 &&
        symlink("../event7", alias) == 0, "create source and stable alias");
    expect(write_text(udev_record,
        "E:ID_VENDOR_ID=1234\nE:ID_MODEL_ID=5678\nE:ID_PATH=platform-test\n") == 0,
        "write narrow udev identity");

    fixture = (struct stat_fixture) {
        .source_path = source, .uinput_path = uinput,
        .source_mode = 0640, .uinput_mode = 0660,
        .owner = 2000, .group = 3000
    };
    {
        static const gid_t service_groups[] = {3000};
        environment = (struct input_proxy_deployment_environment) {
            .sysfs_input_path = sysfs,
            .device_input_path = directory,
            .uinput_path = uinput,
            .udev_data_path = udev,
            .service_uid = 1000,
            .service_gid = 1000,
            .service_groups = service_groups,
            .service_group_count = 1,
            .stat_path = fixture_stat,
            .stat_userdata = &fixture
        };
    }

    request = make_request("ReadinessAccessible");
    request.source_path = source;
    expect(input_proxy_installation_plan_create(&plan, &request, store) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS, "create readiness plan");
    expect(input_proxy_installation_plan_assess(plan, &environment) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS, "assess accessible deployment");
    readiness = input_proxy_installation_plan_readiness(plan);
    expect(readiness != NULL && readiness->physical_source &&
        readiness->source_accessible && readiness->uinput_accessible &&
        readiness->blockers == INPUT_PROXY_DEPLOYMENT_BLOCKER_NONE &&
        readiness->source_permission_remediation ==
            INPUT_PROXY_PERMISSION_REMEDIATION_NOT_REQUIRED,
        "service group access is ready without instance permission rule");
    expect(readiness != NULL && readiness->preferred_source_path != NULL &&
        strcmp(readiness->preferred_source_path, alias) == 0 &&
        readiness->preferred_source_differs,
        "volatile event source reports matching stable alias");
    expect(readiness != NULL &&
        readiness->libinput_status == INPUT_PROXY_LIBINPUT_STATUS_NOT_IGNORED &&
        readiness->libinput_ignore_rule_available,
        "active libinput with narrow identity offers optional ignore rule");
    choices = (struct input_proxy_deployment_choices) {0};
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS,
        "unresolved deployment choices produce a resolution");
    resolution = input_proxy_installation_plan_resolution(plan);
    expect(resolution != NULL && !resolution->choices_resolved &&
        !resolution->application_ready &&
        strcmp(resolution->persistent_source_path, source) == 0,
        "different Preferred run source and optional rule require choices");
    choices.preferred_source = INPUT_PROXY_PREFERRED_SOURCE_USE_PREFERRED;
    choices.libinput_ignore = INPUT_PROXY_REMEDIATION_INSTALL;
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS,
        "choose Preferred run source and optional ignore action");
    resolution = input_proxy_installation_plan_resolution(plan);
    config = input_proxy_installation_plan_config(plan);
    expect(resolution != NULL && resolution->choices_resolved &&
        resolution->application_ready && resolution->libinput_ignore_action &&
        !resolution->source_permission_action &&
        strcmp(resolution->persistent_source_path, alias) == 0 &&
        strcmp(readiness->selected_source_path, alias) == 0 &&
        strcmp(config->source_path, alias) == 0,
        "Preferred run source becomes the persistent runtime source");
    expect(config->activity_timeout_ms == INPUT_PROXY_DEFAULT_ACTIVITY_TIMEOUT_MS &&
        config->detection_throttle_ms ==
            INPUT_PROXY_DEFAULT_DETECTION_THROTTLE_MS &&
        config->running_motion_activity && config->paused_motion_activity &&
        !config->start_paused && strcmp(config->instance_name,
            "ReadinessAccessible") == 0,
        "source selection preserves every other runtime-policy field");
    choices.preferred_source = INPUT_PROXY_PREFERRED_SOURCE_RETAIN_SUPPLIED;
    choices.libinput_ignore = INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL;
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS,
        "revise deployment choices before application");
    resolution = input_proxy_installation_plan_resolution(plan);
    expect(resolution != NULL && resolution->application_ready &&
        !resolution->libinput_ignore_action &&
        strcmp(resolution->persistent_source_path, source) == 0 &&
        strcmp(readiness->selected_source_path, source) == 0 &&
        strcmp(readiness->supplied_source_path, source) == 0 &&
        strcmp(readiness->preferred_source_path, alias) == 0,
        "revised resolution retains diagnostic source facts");
    expect(input_proxy_instance_name_acquire(&ownership,
        "ReadinessAccessible") == INPUT_PROXY_ERROR_INSTANCE_NAME_OWNED,
        "readiness assessment preserves runtime name reservation");
    input_proxy_installation_plan_destroy(plan); plan = NULL;

    request = make_request("StableSupplied"); request.source_path = alias;
    expect(input_proxy_installation_plan_create(&plan, &request, store) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        input_proxy_installation_plan_assess(plan, &environment) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS, "assess supplied stable path");
    readiness = input_proxy_installation_plan_readiness(plan);
    expect(readiness != NULL && strcmp(readiness->selected_source_path, alias) == 0 &&
        readiness->preferred_source_path != NULL &&
        !readiness->preferred_source_differs,
        "stable supplied path remains selected without replacement");
    choices = (struct input_proxy_deployment_choices) {
        .libinput_ignore = INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL
    };
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        input_proxy_installation_plan_resolution(plan)->application_ready &&
        strcmp(input_proxy_installation_plan_config(plan)->source_path, alias) == 0,
        "no different Preferred run source selects supplied source automatically");
    input_proxy_installation_plan_destroy(plan); plan = NULL;

    expect(unlink(alias) == 0, "temporarily remove stable alias");
    request = make_request("NoStableAlias"); request.source_path = source;
    expect(input_proxy_installation_plan_create(&plan, &request, store) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        input_proxy_installation_plan_assess(plan, &environment) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS, "assess source without stable alias");
    readiness = input_proxy_installation_plan_readiness(plan);
    expect(readiness != NULL && readiness->preferred_source_path == NULL &&
        readiness->blockers == INPUT_PROXY_DEPLOYMENT_BLOCKER_NONE,
        "absence of stable alias is not a blocker");
    choices = (struct input_proxy_deployment_choices) {
        .libinput_ignore = INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL
    };
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        input_proxy_installation_plan_resolution(plan)->application_ready,
        "missing Preferred run source requires no source choice");
    input_proxy_installation_plan_destroy(plan); plan = NULL;
    expect(symlink("../event7", alias) == 0, "restore stable alias");

    fixture.source_mode = 0600;
    request = make_request("PermissionAvailable"); request.source_path = source;
    expect(input_proxy_installation_plan_create(&plan, &request, store) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        input_proxy_installation_plan_assess(plan, &environment) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS, "assess missing source access");
    readiness = input_proxy_installation_plan_readiness(plan);
    expect(readiness != NULL && !readiness->source_accessible &&
        readiness->source_permission_remediation ==
            INPUT_PROXY_PERMISSION_REMEDIATION_AVAILABLE &&
        (readiness->blockers & INPUT_PROXY_DEPLOYMENT_BLOCKER_SOURCE_PERMISSION) == 0,
        "narrow match makes targeted source remediation available");
    choices = (struct input_proxy_deployment_choices) {
        .preferred_source = INPUT_PROXY_PREFERRED_SOURCE_RETAIN_SUPPLIED,
        .source_permission = INPUT_PROXY_REMEDIATION_INSTALL,
        .libinput_ignore = INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL
    };
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS,
        "resolve targeted source permission installation");
    resolution = input_proxy_installation_plan_resolution(plan);
    expect(resolution != NULL && resolution->application_ready &&
        resolution->source_permission_action &&
        !resolution->libinput_ignore_action,
        "permission installation plans an independent instance-owned action");
    choices.source_permission = INPUT_PROXY_REMEDIATION_DO_NOT_INSTALL;
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS,
        "decline targeted source permission installation");
    resolution = input_proxy_installation_plan_resolution(plan);
    expect(resolution != NULL && resolution->choices_resolved &&
        !resolution->application_ready && !resolution->source_permission_action,
        "declining required permission remediation remains non-ready");
    input_proxy_installation_plan_destroy(plan); plan = NULL;

    snprintf(path, sizeof(path), "%s/id/bustype", device_dir);
    expect(write_text(path, "0018\n") == 0, "write I2C fixture bus");
    snprintf(path, sizeof(path), "%s/id/vendor", device_dir);
    expect(write_text(path, "0416\n") == 0, "write I2C fixture vendor");
    snprintf(path, sizeof(path), "%s/id/product", device_dir);
    expect(write_text(path, "038f\n") == 0, "write I2C fixture product");
    expect(write_text(udev_record, "E:ID_PATH=platform-test-i2c\n") == 0,
        "write I2C udev path without USB identifiers");
    request = make_request("I2cIdentity"); request.source_path = source;
    expect(input_proxy_installation_plan_create(&plan, &request, store) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        input_proxy_installation_plan_assess(plan, &environment) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS,
        "assess I2C source using kernel input identity");
    readiness = input_proxy_installation_plan_readiness(plan);
    expect(readiness != NULL && !readiness->source_accessible &&
        readiness->source_permission_remediation ==
            INPUT_PROXY_PERMISSION_REMEDIATION_AVAILABLE &&
        readiness->libinput_ignore_rule_available &&
        (readiness->blockers &
            INPUT_PROXY_DEPLOYMENT_BLOCKER_SOURCE_PERMISSION) == 0,
        "I2C bus/vendor/product plus ID_PATH supports both remediations");
    input_proxy_installation_plan_destroy(plan); plan = NULL;
    snprintf(path, sizeof(path), "%s/id/bustype", device_dir);
    expect(write_text(path, "0003\n") == 0, "restore USB fixture bus");
    snprintf(path, sizeof(path), "%s/id/vendor", device_dir);
    expect(unlink(path) == 0, "remove I2C fixture vendor");
    snprintf(path, sizeof(path), "%s/id/product", device_dir);
    expect(unlink(path) == 0, "remove I2C fixture product");

    expect(write_text(udev_record, "E:ID_VENDOR_ID=1234\n") == 0,
        "remove narrow udev identity");
    request = make_request("PermissionBlocked"); request.source_path = source;
    expect(input_proxy_installation_plan_create(&plan, &request, store) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        input_proxy_installation_plan_assess(plan, &environment) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS, "assess unmatched source");
    readiness = input_proxy_installation_plan_readiness(plan);
    expect(readiness != NULL &&
        readiness->source_permission_remediation ==
            INPUT_PROXY_PERMISSION_REMEDIATION_UNAVAILABLE &&
        (readiness->blockers & INPUT_PROXY_DEPLOYMENT_BLOCKER_SOURCE_PERMISSION) != 0 &&
        !readiness->libinput_ignore_rule_available,
        "missing narrow match blocks permission repair and ignore offer");
    choices = (struct input_proxy_deployment_choices) {
        .preferred_source = INPUT_PROXY_PREFERRED_SOURCE_RETAIN_SUPPLIED,
        .source_permission = INPUT_PROXY_REMEDIATION_INSTALL
    };
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_INVALID_CHOICES &&
        input_proxy_installation_plan_resolution(plan) == NULL,
        "choice cannot force unavailable permission remediation");
    choices.source_permission = INPUT_PROXY_REMEDIATION_UNRESOLVED;
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        !input_proxy_installation_plan_resolution(plan)->application_ready,
        "unavailable permission remediation leaves blocker intact");
    input_proxy_installation_plan_destroy(plan); plan = NULL;

    fixture.source_mode = 0640; fixture.uinput_mode = 0600;
    request = make_request("UinputBlocked"); request.source_path = source;
    expect(input_proxy_installation_plan_create(&plan, &request, store) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        input_proxy_installation_plan_assess(plan, &environment) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS, "assess missing uinput access");
    readiness = input_proxy_installation_plan_readiness(plan);
    expect(readiness != NULL && !readiness->uinput_accessible &&
        (readiness->blockers & INPUT_PROXY_DEPLOYMENT_BLOCKER_UINPUT) != 0,
        "missing package-owned uinput access is a blocker");
    choices = (struct input_proxy_deployment_choices) {
        .preferred_source = INPUT_PROXY_PREFERRED_SOURCE_RETAIN_SUPPLIED
    };
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        !input_proxy_installation_plan_resolution(plan)->application_ready,
        "deployment choices cannot clear package-owned uinput blocker");
    input_proxy_installation_plan_destroy(plan); plan = NULL;

    fixture.uinput_mode = 0660;
    expect(write_text(udev_record,
        "E:ID_VENDOR_ID=1234\nE:ID_MODEL_ID=5678\nE:ID_PATH=platform-test\nE:LIBINPUT_IGNORE_DEVICE=1\n") == 0,
        "mark fixture ignored by libinput");
    request = make_request("AlreadyIgnored"); request.source_path = source;
    expect(input_proxy_installation_plan_create(&plan, &request, store) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        input_proxy_installation_plan_assess(plan, &environment) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS, "assess ignored source");
    readiness = input_proxy_installation_plan_readiness(plan);
    expect(readiness != NULL &&
        readiness->libinput_status == INPUT_PROXY_LIBINPUT_STATUS_IGNORED &&
        !readiness->libinput_ignore_rule_available,
        "already ignored source requires no ignore rule");
    choices = (struct input_proxy_deployment_choices) {
        .preferred_source = INPUT_PROXY_PREFERRED_SOURCE_RETAIN_SUPPLIED
    };
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_SUCCESS &&
        input_proxy_installation_plan_resolution(plan)->application_ready &&
        !input_proxy_installation_plan_resolution(plan)->libinput_ignore_action,
        "already ignored source needs no libinput choice or new action");
    choices.libinput_ignore = INPUT_PROXY_REMEDIATION_INSTALL;
    expect(input_proxy_installation_plan_resolve(plan, &choices) ==
        INPUT_PROXY_INSTALLATION_PLAN_INVALID_CHOICES &&
        input_proxy_installation_plan_resolution(plan)->application_ready,
        "invalid repeated resolution preserves the previous valid resolution");
    input_proxy_installation_plan_destroy(plan); plan = NULL;

    request = make_request("PlanDefaults");
    result = input_proxy_installation_plan_create(&plan, &request, store);
    expect(result == INPUT_PROXY_INSTALLATION_PLAN_SUCCESS,
        "defaulted request plans successfully");
    config = input_proxy_installation_plan_config(plan);
    expect(config != NULL &&
        strcmp(config->source_path, request.source_path) == 0 &&
        strcmp(config->instance_name, request.instance_name) == 0,
        "plan owns resolved source and name");
    expect(config != NULL &&
        config->activity_timeout_ms == INPUT_PROXY_DEFAULT_ACTIVITY_TIMEOUT_MS &&
        config->detection_throttle_ms ==
            INPUT_PROXY_DEFAULT_DETECTION_THROTTLE_MS &&
        config->running_motion_activity && config->paused_motion_activity &&
        !config->start_paused && !config->verbose,
        "plan contains the complete persistent default snapshot");
    expect(input_proxy_instance_name_acquire(
        &ownership,
        "PlanDefaults"
    ) == INPUT_PROXY_ERROR_INSTANCE_NAME_OWNED,
        "successful plan retains runtime name reservation");
    input_proxy_installation_plan_release_runtime_name(plan);
    input_proxy_installation_plan_release_runtime_name(plan);
    expect(input_proxy_installation_plan_config(plan) == config &&
        strcmp(config->instance_name, "PlanDefaults") == 0,
        "reservation release is idempotent and preserves the resolved plan");
    expect(input_proxy_instance_name_acquire(
        &ownership,
        "PlanDefaults"
    ) == INPUT_PROXY_SUCCESS,
        "explicit activation-boundary operation releases reservation");
    input_proxy_instance_name_release(ownership);
    ownership = NULL;
    input_proxy_installation_plan_destroy(plan);
    plan = NULL;
    expect(input_proxy_instance_name_acquire(
        &ownership,
        "PlanDefaults"
    ) == INPUT_PROXY_SUCCESS, "destroying plan releases reservation");
    input_proxy_instance_name_release(ownership);
    ownership = NULL;

    request = make_request("ExplicitPolicy");
    request.activity_timeout_ms = 7;
    request.detection_throttle_ms = 9;
    request.running_motion_activity = false;
    request.paused_motion_activity = false;
    request.start_paused = true;
    request.activity_timeout_supplied = true;
    request.detection_throttle_supplied = true;
    request.running_motion_supplied = true;
    request.paused_motion_supplied = true;
    request.start_paused_supplied = true;
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_SUCCESS, "explicit policy resolves");
    config = input_proxy_installation_plan_config(plan);
    expect(config->activity_timeout_ms == 7 &&
        config->detection_throttle_ms == 9 &&
        !config->running_motion_activity &&
        !config->paused_motion_activity && config->start_paused,
        "explicit policy is preserved");
    input_proxy_installation_plan_destroy(plan);
    plan = NULL;

    expect(input_proxy_runtime_policy_parse_duration("0", &duration) &&
        duration == 0, "duration accepts zero");
    expect(input_proxy_runtime_policy_parse_duration("4294967295", &duration) &&
        duration == UINT32_MAX, "duration accepts maximum");
    expect(!input_proxy_runtime_policy_parse_duration("4294967296", &duration) &&
        !input_proxy_runtime_policy_parse_duration("-1", &duration) &&
        !input_proxy_runtime_policy_parse_duration("1x", &duration),
        "duration rejects out-of-range and non-digit values");
    expect(input_proxy_runtime_policy_parse_on_off("on", &enabled) && enabled &&
        input_proxy_runtime_policy_parse_on_off("off", &enabled) && !enabled &&
        !input_proxy_runtime_policy_parse_on_off("true", &enabled),
        "on-off grammar is authoritative");

    request = make_request("Invalid Name");
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_INVALID_INSTANCE_NAME,
        "planner shares Instance Name validation");

    request = make_request("InstalledCollision");
    {
        struct input_proxy_session_config artifact_config = {
            .source_path = request.source_path,
            .instance_name = request.instance_name,
            .activity_timeout_ms = request.activity_timeout_ms,
            .detection_throttle_ms = request.detection_throttle_ms,
            .running_motion_activity = request.running_motion_activity,
            .paused_motion_activity = request.paused_motion_activity
        };
        expect(input_proxy_installed_instance_create(store, &artifact_config) ==
            INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS, "create occupied artifact");
    }
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_INSTALLED_NAME_COLLISION,
        "occupied artifact reports installed-name collision");
    expect(input_proxy_instance_name_acquire(
        &ownership,
        "InstalledCollision"
    ) == INPUT_PROXY_SUCCESS, "installed collision does not reserve runtime name");
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_INSTALLED_NAME_COLLISION,
        "installed collision takes precedence when runtime name is also owned");
    input_proxy_instance_name_release(ownership);
    ownership = NULL;

    request = make_request("RuntimeCollision");
    expect(input_proxy_instance_name_acquire(
        &ownership,
        request.instance_name
    ) == INPUT_PROXY_SUCCESS, "hold runtime name directly");
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_RUNTIME_NAME_COLLISION,
        "owned socket reports runtime-name collision");
    input_proxy_instance_name_release(ownership);
    ownership = NULL;
    expect(input_proxy_installation_plan_create(
        &plan,
        &request,
        store
    ) == INPUT_PROXY_INSTALLATION_PLAN_SUCCESS,
        "runtime collision failure leaves no reservation");
    input_proxy_installation_plan_destroy(plan);

    expect(input_proxy_installed_instance_remove(
        store,
        "InstalledCollision"
    ) == INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS, "remove test artifact");
    input_proxy_installed_instance_store_destroy(store);
    expect(unlink(alias) == 0 && unlink(source) == 0 &&
        unlink(udev_record) == 0, "remove readiness fixture links and udev data");
    snprintf(path, sizeof(path), "%s/name", device_dir); expect(unlink(path) == 0, "remove fixture name");
    snprintf(path, sizeof(path), "%s/id/bustype", device_dir); expect(unlink(path) == 0, "remove fixture bus");
    snprintf(path, sizeof(path), "%s/capabilities/key", device_dir); expect(unlink(path) == 0, "remove fixture keys");
    snprintf(path, sizeof(path), "%s/capabilities/abs", device_dir); expect(unlink(path) == 0, "remove fixture abs");
    snprintf(path, sizeof(path), "%s/capabilities/rel", device_dir); expect(unlink(path) == 0, "remove fixture rel");
    snprintf(path, sizeof(path), "%s/properties", device_dir); expect(unlink(path) == 0, "remove fixture properties");
    snprintf(path, sizeof(path), "%s/dev", event_sysfs); expect(unlink(path) == 0, "remove fixture device number");
    snprintf(path, sizeof(path), "%s/capabilities", device_dir); expect(rmdir(path) == 0, "remove fixture capabilities directory");
    snprintf(path, sizeof(path), "%s/id", device_dir); expect(rmdir(path) == 0, "remove fixture identity directory");
    expect(rmdir(device_dir) == 0 && rmdir(event_sysfs) == 0 &&
        rmdir(sysfs) == 0 && rmdir(alias_dir) == 0 && rmdir(udev) == 0,
        "remove readiness fixture directories");
    expect(rmdir(directory) == 0, "remove isolated artifact directory");

    if (failures != 0) {
        fprintf(stderr, "%d installation planner test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
