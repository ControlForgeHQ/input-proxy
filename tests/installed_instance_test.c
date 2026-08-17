#define _POSIX_C_SOURCE 200809L

#include "installed_instance_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void expect(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static bool write_file(const char *path, const char *content)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    size_t length = strlen(content);
    ssize_t result;

    if (descriptor < 0) {
        return false;
    }
    result = write(descriptor, content, length);
    return close(descriptor) == 0 && result == (ssize_t)length;
}

static char *read_file(const char *path)
{
    struct stat status;
    char *content;
    int descriptor = open(path, O_RDONLY);
    ssize_t result;

    if (descriptor < 0 || fstat(descriptor, &status) != 0) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        return NULL;
    }
    content = malloc((size_t)status.st_size + 1);
    if (content == NULL) {
        close(descriptor);
        return NULL;
    }
    result = read(descriptor, content, (size_t)status.st_size);
    close(descriptor);
    if (result != status.st_size) {
        free(content);
        return NULL;
    }
    content[result] = '\0';
    return content;
}

int main(void)
{
    char directory[] = "/tmp/input-proxy-installed-test-XXXXXX";
    struct input_proxy_installed_instance_store *store = NULL;
    struct input_proxy_installed_instance_list list = { 0 };
    struct input_proxy_session_config config = {
        .source_path = "/dev/input/by-path/platform-test event",
        .instance_name = "DSI-Touch",
        .activity_timeout_ms = 5000,
        .detection_throttle_ms = 250,
        .running_motion_activity = true,
        .paused_motion_activity = false,
        .start_paused = true,
        .verbose = true
    };
    const char *expected =
        "--source\n/dev/input/by-path/platform-test event\n"
        "--name\nDSI-Touch\n"
        "--activity-timeout-ms\n5000\n"
        "--detection-throttle-ms\n250\n"
        "--running-motion-activity\non\n"
        "--paused-motion-activity\noff\n"
        "--start-paused\non\n";
    char path[512];
    char *derived_path = NULL;
    char *content;
    bool exists = false;
    struct stat status;

    expect(mkdtemp(directory) != NULL, "create temporary directory");
    expect(input_proxy_installed_instance_store_create_for_directory(
        &store, directory) == INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS,
        "create isolated store");
    expect(input_proxy_installed_instance_enumerate(store, &list) ==
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS && list.count == 0,
        "empty directory produces an empty instance set");
    input_proxy_installed_instance_list_destroy(&list);
    expect(input_proxy_installed_instance_path(
        store, "DSI-Touch", &derived_path) ==
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS,
        "derive artifact path");
    snprintf(path, sizeof(path), "%s/DSI-Touch.args", directory);
    expect(derived_path != NULL && strcmp(derived_path, path) == 0,
        "artifact path uses InstanceName.args");
    free(derived_path);
    expect(input_proxy_installed_instance_path(store, ".invalid", &derived_path) ==
        INPUT_PROXY_INSTALLED_INSTANCE_INVALID_NAME,
        "reject invalid Instance Name path");

    expect(input_proxy_installed_instance_create(store, &config) ==
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS, "create artifact");
    content = read_file(path);
    expect(content != NULL && strcmp(content, expected) == 0,
        "serialize complete runtime policy without verbose");
    free(content);
    expect(stat(path, &status) == 0 && (status.st_mode & 0777) == 0640,
        "artifact uses conservative permissions");
    expect(input_proxy_installed_instance_exists(store, "DSI-Touch", &exists) ==
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS && exists,
        "created artifact exists");

    config.source_path = "/changed";
    expect(input_proxy_installed_instance_create(store, &config) ==
        INPUT_PROXY_INSTALLED_INSTANCE_ALREADY_INSTALLED,
        "refuse overwrite");
    content = read_file(path);
    expect(content != NULL && strcmp(content, expected) == 0,
        "failed overwrite preserves original bytes");
    free(content);
    config.source_path = "/invalid\npath";
    config.instance_name = "New-Instance";
    expect(input_proxy_installed_instance_create(store, &config) ==
        INPUT_PROXY_INSTALLED_INSTANCE_INVALID_ARGUMENT,
        "reject policy values that the line-oriented format cannot represent");
    config.instance_name = "DSI-Touch";

    snprintf(path, sizeof(path), "%s/USB-Touch.args", directory);
    expect(write_file(path, "fixture"), "create USB fixture");
    snprintf(path, sizeof(path), "%s/Buttons_1.args", directory);
    expect(write_file(path, "fixture"), "create buttons fixture");
    snprintf(path, sizeof(path), "%s/README", directory);
    expect(write_file(path, "fixture"), "create unrelated fixture");
    snprintf(path, sizeof(path), "%s/DSI-Touch.tmp", directory);
    expect(write_file(path, "fixture"), "create temporary-looking fixture");
    snprintf(path, sizeof(path), "%s/.invalid.args", directory);
    expect(write_file(path, "fixture"), "create invalid-name fixture");
    snprintf(path, sizeof(path), "%s/not-an-instance.conf", directory);
    expect(write_file(path, "fixture"), "create config fixture");
    snprintf(path, sizeof(path), "%s/Subdirectory.args", directory);
    expect(mkdir(path, 0700) == 0, "create suffix-matching subdirectory");
    expect(input_proxy_installed_instance_exists(
        store, "Subdirectory", &exists) ==
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS && exists,
        "non-regular final path occupies the Instance Name");

    expect(input_proxy_installed_instance_enumerate(store, &list) ==
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS, "enumerate artifacts");
    expect(list.count == 3, "enumerate exactly three instances");
    expect(list.count == 3 && strcmp(list.names[0], "Buttons_1") == 0 &&
        strcmp(list.names[1], "DSI-Touch") == 0 &&
        strcmp(list.names[2], "USB-Touch") == 0,
        "enumeration is valid and deterministic");
    input_proxy_installed_instance_list_destroy(&list);

    expect(input_proxy_installed_instance_remove(store, "DSI-Touch") ==
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS, "remove artifact");
    expect(input_proxy_installed_instance_remove(store, "DSI-Touch") ==
        INPUT_PROXY_INSTALLED_INSTANCE_NOT_FOUND, "report missing removal");
    expect(input_proxy_installed_instance_exists(store, "USB-Touch", &exists) ==
        INPUT_PROXY_INSTALLED_INSTANCE_SUCCESS && exists,
        "removal leaves other artifacts unchanged");

    input_proxy_installed_instance_store_destroy(store);
    snprintf(path, sizeof(path), "%s/USB-Touch.args", directory);
    unlink(path);
    snprintf(path, sizeof(path), "%s/Buttons_1.args", directory);
    unlink(path);
    snprintf(path, sizeof(path), "%s/README", directory);
    unlink(path);
    snprintf(path, sizeof(path), "%s/DSI-Touch.tmp", directory);
    unlink(path);
    snprintf(path, sizeof(path), "%s/.invalid.args", directory);
    unlink(path);
    snprintf(path, sizeof(path), "%s/not-an-instance.conf", directory);
    unlink(path);
    snprintf(path, sizeof(path), "%s/Subdirectory.args", directory);
    rmdir(path);
    rmdir(directory);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
