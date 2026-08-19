#define _POSIX_C_SOURCE 200809L

#include "uninstall_command_internal.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

struct fixture {
    enum input_proxy_uninstall_stage_result stop_result;
    enum input_proxy_uninstall_stage_result disable_result;
    bool source_present;
    bool reload_ok;
    bool trigger_ok;
    bool settle_ok;
    bool fail_response_remove;
    int stops, disables, reloads, triggers, settles;
    char triggered_source[256];
};

static void expect(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); failures++; }
}

static enum input_proxy_uninstall_stage_result stop_service(
    const char *unit, void *userdata)
{
    struct fixture *fixture = userdata;
    expect(strcmp(unit, "input-proxy@Test-One.service") == 0,
        "target only selected service");
    fixture->stops++;
    return fixture->stop_result;
}

static enum input_proxy_uninstall_stage_result disable_service(
    const char *unit, void *userdata)
{
    struct fixture *fixture = userdata;
    (void)unit; fixture->disables++; return fixture->disable_result;
}

static enum input_proxy_uninstall_stage_result remove_file(
    const char *path, void *userdata)
{
    struct fixture *fixture = userdata;
    if (fixture->fail_response_remove && strstr(path, ".args") != NULL)
        return INPUT_PROXY_UNINSTALL_STAGE_FAILED;
    if (unlink(path) == 0) return INPUT_PROXY_UNINSTALL_STAGE_SUCCESS;
    return INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED;
}

static bool source_present(const char *source, void *userdata)
{
    struct fixture *fixture = userdata;
    expect(strcmp(source, "/dev/input/by-path/test-source") == 0,
        "read source from response artifact");
    return fixture->source_present;
}

static bool reload_udev(void *userdata)
{
    struct fixture *fixture = userdata; fixture->reloads++;
    return fixture->reload_ok;
}

static bool trigger_source(const char *source, void *userdata)
{
    struct fixture *fixture = userdata; fixture->triggers++;
    snprintf(fixture->triggered_source, sizeof(fixture->triggered_source), "%s", source);
    return fixture->trigger_ok;
}

static bool settle_udev(void *userdata)
{
    struct fixture *fixture = userdata; fixture->settles++;
    return fixture->settle_ok;
}

static bool write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file == NULL) return false;
    return fputs(text, file) >= 0 && fclose(file) == 0;
}

static bool exists(const char *path) { return access(path, F_OK) == 0; }

static int invoke(const char *directory, struct fixture *fixture,
    bool interactive, FILE *input, const char *name, char **output_text,
    char **error_text)
{
    char *argv_named[] = { "input-proxy", "uninstall", (char *)name };
    char *argv_select[] = { "input-proxy", "uninstall" };
    size_t output_size = 0, error_size = 0;
    FILE *output = open_memstream(output_text, &output_size);
    FILE *error = open_memstream(error_text, &error_size);
    const struct input_proxy_uninstall_operations operations = {
        .stop_service = stop_service, .disable_service = disable_service,
        .remove_file = remove_file, .source_present = source_present,
        .reload_udev = reload_udev, .trigger_source = trigger_source,
        .settle_udev = settle_udev, .userdata = fixture
    };
    const struct input_proxy_uninstall_command_environment environment = {
        .effective_uid = 0, .interactive = interactive, .input = input,
        .output = output, .error = error,
        .installed_instance_directory = directory,
        .udev_rule_directory = directory, .operations = &operations
    };
    int result = input_proxy_uninstall_command_with_environment(
        name == NULL ? 2 : 3, name == NULL ? argv_select : argv_named,
        &environment);
    fclose(output); fclose(error);
    return result;
}

static void create_artifacts(const char *directory, bool rule)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/Test-One.args", directory);
    expect(write_text(path, "--source\n/dev/input/by-path/test-source\n--name\nTest-One\n"),
        "create response artifact");
    if (rule) {
        snprintf(path, sizeof(path), "%s/90-input-proxy-Test-One.rules", directory);
        expect(write_text(path, "fixture\n"), "create rule artifact");
    }
}

int main(void)
{
    char directory[] = "/tmp/input-proxy-uninstall-test-XXXXXX";
    char response[512], rule[512], other[512];
    struct fixture fixture;
    char *output = NULL, *error = NULL;
    FILE *selection;

    expect(mkdtemp(directory) != NULL, "create fixture directory");
    snprintf(response, sizeof(response), "%s/Test-One.args", directory);
    snprintf(rule, sizeof(rule), "%s/90-input-proxy-Test-One.rules", directory);
    snprintf(other, sizeof(other), "%s/Other.args", directory);

    fixture = (struct fixture) {
        .stop_result = INPUT_PROXY_UNINSTALL_STAGE_SUCCESS,
        .disable_result = INPUT_PROXY_UNINSTALL_STAGE_SUCCESS,
        .source_present = true, .reload_ok = true, .trigger_ok = true,
        .settle_ok = true
    };
    create_artifacts(directory, true);
    expect(invoke(directory, &fixture, false, stdin, "Test-One", &output, &error)
        == EXIT_SUCCESS, "complete installation uninstalls");
    expect(!exists(response) && !exists(rule), "remove rule and response artifact");
    expect(fixture.stops == 1 && fixture.disables == 1 && fixture.reloads == 1 &&
        fixture.triggers == 1 && fixture.settles == 1, "run complete teardown sequence");
    expect(strstr(output, "has been uninstalled") != NULL && error[0] == '\0',
        "report successful uninstall");
    free(output); free(error); output = error = NULL;

    fixture = (struct fixture) {
        .stop_result = INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED,
        .disable_result = INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED,
        .reload_ok = true, .trigger_ok = true, .settle_ok = true
    };
    create_artifacts(directory, false);
    expect(invoke(directory, &fixture, false, stdin, "Test-One", &output, &error)
        == EXIT_SUCCESS, "response-only partial installation uninstalls");
    expect(fixture.reloads == 1 && strstr(output, "already inactive") != NULL &&
        strstr(output, "rule not present") != NULL, "report no-op teardown stages");
    free(output); free(error); output = error = NULL;

    fixture = (struct fixture) {
        .stop_result = INPUT_PROXY_UNINSTALL_STAGE_FAILED,
        .disable_result = INPUT_PROXY_UNINSTALL_STAGE_SUCCESS,
        .source_present = false, .reload_ok = true, .trigger_ok = true,
        .settle_ok = true
    };
    create_artifacts(directory, true);
    expect(invoke(directory, &fixture, false, stdin, "Test-One", &output, &error)
        == EXIT_FAILURE, "service failure makes uninstall incomplete");
    expect(exists(response) && !exists(rule) && fixture.reloads == 1 &&
        fixture.triggers == 0, "continue safe cleanup and retain response artifact");
    expect(strstr(error, "Service stop failed") != NULL &&
        strstr(output, "retained for retry") != NULL, "report failure and retry state");
    unlink(response); free(output); free(error); output = error = NULL;

    fixture = (struct fixture) {
        .stop_result = INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED,
        .disable_result = INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED,
        .reload_ok = false, .trigger_ok = true, .settle_ok = true
    };
    create_artifacts(directory, true);
    expect(invoke(directory, &fixture, false, stdin, "Test-One", &output, &error)
        == EXIT_FAILURE && exists(response), "udev reload failure retains response");
    unlink(response); free(output); free(error); output = error = NULL;

    fixture = (struct fixture) {
        .stop_result = INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED,
        .disable_result = INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED,
        .reload_ok = true, .trigger_ok = true, .settle_ok = true,
        .fail_response_remove = true
    };
    create_artifacts(directory, false);
    expect(invoke(directory, &fixture, false, stdin, "Test-One", &output, &error)
        == EXIT_FAILURE && exists(response), "response removal failure is reported");
    unlink(response); free(output); free(error); output = error = NULL;

    expect(write_text(response, "--source\n/dev/input/by-path/test-source\n"),
        "create selectable response");
    expect(write_text(other, "fixture\n"), "create other instance");
    fixture = (struct fixture) {
        .stop_result = INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED,
        .disable_result = INPUT_PROXY_UNINSTALL_STAGE_NOT_REQUIRED,
        .reload_ok = true, .trigger_ok = true, .settle_ok = true
    };
    expect(invoke(directory, &fixture, false, stdin, NULL, &output, &error)
        == EXIT_FAILURE, "non-interactive selection fails clearly");
    expect(exists(response) && exists(other) && fixture.stops == 0,
        "non-interactive selection performs no mutation");
    expect(strstr(error, "standard input is not interactive") != NULL,
        "report unavailable interactive selection");
    free(output); free(error); output = error = NULL;
    selection = fmemopen("2\n", 2, "r");
    expect(invoke(directory, &fixture, true, selection, NULL, &output, &error)
        == EXIT_SUCCESS, "interactive selection uses enumerated artifacts");
    fclose(selection);
    expect(!exists(response) && exists(other), "selection leaves other instance untouched");
    unlink(other); free(output); free(error); output = error = NULL;

    expect(invoke(directory, &fixture, false, stdin, NULL, &output, &error)
        == EXIT_SUCCESS && strstr(output, "No Installed Instances") != NULL,
        "empty store exits without mutation");
    free(output); free(error);
    rmdir(directory);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
