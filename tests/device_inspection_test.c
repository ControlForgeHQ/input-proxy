#define _XOPEN_SOURCE 700

#include "device_inspection_internal.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int remove_entry(const char *path, const struct stat *status, int type,
                        struct FTW *walk)
{
    (void)status; (void)type; (void)walk;
    return remove(path);
}

static int make_directory(const char *path) { return mkdir(path, 0700) != 0; }

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    if (file == NULL) return 1;
    return fputs(text, file) == EOF || fclose(file) != 0;
}

int main(void)
{
    char root_template[] = "/tmp/input-proxy-inspection-test-XXXXXX";
    char *root = mkdtemp(root_template);
    char sysfs[512], event[512], path[1024], udev[512];
    char by_id_path[1024], by_path_path[1024];
    char *output = NULL, *error = NULL;
    size_t output_size = 0, error_size = 0;
    FILE *stream, *error_stream;
    enum input_proxy_result result;
    int failures = 0;

    if (root == NULL) return 1;
    snprintf(sysfs, sizeof(sysfs), "%s/sys", root);
    snprintf(event, sizeof(event), "%s/event7", sysfs);
    snprintf(udev, sizeof(udev), "%s/udev", root);
    failures += make_directory(sysfs);
    failures += make_directory(event);
    snprintf(path, sizeof(path), "%s/device", event); failures += make_directory(path);
    snprintf(path, sizeof(path), "%s/device/capabilities", event); failures += make_directory(path);
    snprintf(path, sizeof(path), "%s/device/id", event); failures += make_directory(path);
    snprintf(path, sizeof(path), "%s/device/name", event); failures += write_text(path, "Fixture Keyboard\n");
    snprintf(path, sizeof(path), "%s/device/id/bustype", event); failures += write_text(path, "0003\n");
    snprintf(path, sizeof(path), "%s/device/capabilities/key", event); failures += write_text(path, "0\n");
    snprintf(path, sizeof(path), "%s/device/capabilities/abs", event); failures += write_text(path, "0\n");
    snprintf(path, sizeof(path), "%s/device/capabilities/rel", event); failures += write_text(path, "0\n");
    snprintf(path, sizeof(path), "%s/device/properties", event); failures += write_text(path, "0\n");
    snprintf(path, sizeof(path), "%s/dev", event); failures += write_text(path, "1:3\n");
    failures += make_directory(udev);
    snprintf(path, sizeof(path), "%s/event7", root);
    if (symlink("/dev/null", path) != 0) failures++;
    {
        char alias_directory[1024];
        snprintf(alias_directory, sizeof(alias_directory), "%s/by-id", root);
        failures += make_directory(alias_directory);
        snprintf(by_id_path, sizeof(by_id_path), "%s/by-id/test-device", root);
        if (symlink("../event7", by_id_path) != 0) failures++;
        snprintf(alias_directory, sizeof(alias_directory), "%s/by-path", root);
        failures += make_directory(alias_directory);
        snprintf(by_path_path, sizeof(by_path_path), "%s/by-path/test-port", root);
        if (symlink("../event7", by_path_path) != 0) failures++;
    }

    stream = open_memstream(&output, &output_size);
    error_stream = open_memstream(&error, &error_size);
    if (failures || stream == NULL || error_stream == NULL) return 1;
    result = input_proxy_inspect_device(stream, error_stream, path, sysfs, root,
                                        "/dev/null", udev);
    fclose(stream); fclose(error_stream);
    if (result != INPUT_PROXY_SUCCESS ||
        strstr(output, "Device identity") == NULL ||
        strstr(output, "Event node:") == NULL ||
        strstr(output, path) == NULL ||
        strstr(output, "Preferred run source:") == NULL ||
        strstr(output, "/by-id/test-device") == NULL ||
        strstr(output, "Fixture Keyboard") == NULL ||
        strstr(output, "Bus:                   USB") == NULL ||
        strstr(output, "Source readable:       No (BLOCKER)") == NULL ||
        strstr(output, "/dev/uinput writable:  Yes") == NULL ||
        strstr(output, "No udev rule suggested") == NULL ||
        strstr(output, "Suggested udev rule") != NULL ||
        strstr(output, "(not applied)") != NULL ||
        strstr(output, "(not run)") != NULL ||
        strstr(output, "BLOCKED:") == NULL ||
        strstr(output, "Suggested command") == NULL ||
        strstr(output, "--source ") == NULL ||
        strstr(output, "YOUR DEVICE NAME") == NULL ||
        strstr(output, "\033[") != NULL || error[0] != '\0') {
        fprintf(stderr, "unexpected inspection result:\n%s%s", output, error);
        failures++;
    }
    free(output); free(error);
    {
        const char *const aliases[] = {by_id_path, by_path_path};
        size_t alias_index;
        for (alias_index = 0; alias_index < 2; ++alias_index) {
            output = NULL; error = NULL; output_size = 0; error_size = 0;
            stream = open_memstream(&output, &output_size);
            error_stream = open_memstream(&error, &error_size);
            if (stream == NULL || error_stream == NULL) return 1;
            result = input_proxy_inspect_device(stream, error_stream,
                aliases[alias_index], sysfs, root, "/dev/null", udev);
            fclose(stream); fclose(error_stream);
            if (result != INPUT_PROXY_SUCCESS ||
                strstr(output, aliases[alias_index]) == NULL ||
                strstr(output, "Event node:") == NULL ||
                strstr(output, path) == NULL || error[0] != '\0') {
                fprintf(stderr, "unexpected alias inspection result:\n%s%s",
                        output, error);
                failures++;
            }
            free(output); free(error);
        }
    }
    output = NULL;
    output_size = 0;
    stream = open_memstream(&output, &output_size);
    if (stream == NULL) return 1;
    {
        const char *const values[] = {
            "KEY_ESC", "KEY_1", "KEY_2", "KEY_3", "KEY_4", "KEY_5",
            "KEY_6", "KEY_7", "KEY_8", "KEY_9", "KEY_0", "KEY_MINUS"
        };
        input_proxy_print_wrapped_values(stream, "Keys/buttons:", values,
                                         sizeof(values) / sizeof(values[0]));
    }
    fclose(stream);
    if (strstr(output, "\n                         KEY_") == NULL ||
        strstr(output, "\033[") != NULL) {
        fprintf(stderr, "unexpected wrapped output:\n%s", output);
        failures++;
    }
    free(output);
    if (nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0) failures++;
    return failures == 0 ? 0 : 1;
}
