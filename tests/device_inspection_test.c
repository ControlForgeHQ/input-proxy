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
    failures += make_directory(udev);
    snprintf(path, sizeof(path), "%s/event7", root);
    if (symlink("/dev/null", path) != 0) failures++;

    stream = open_memstream(&output, &output_size);
    error_stream = open_memstream(&error, &error_size);
    if (failures || stream == NULL || error_stream == NULL) return 1;
    result = input_proxy_inspect_device(stream, error_stream, path, sysfs,
                                        "/dev/null", udev);
    fclose(stream); fclose(error_stream);
    if (result != INPUT_PROXY_SUCCESS ||
        strstr(output, "Device identity") == NULL ||
        strstr(output, "Fixture Keyboard") == NULL ||
        strstr(output, "Bus:                   USB") == NULL ||
        strstr(output, "Source readable:       No (BLOCKER)") == NULL ||
        strstr(output, "/dev/uinput writable:  Yes") == NULL ||
        strstr(output, "No udev rule suggested") == NULL ||
        strstr(output, "Suggested udev rule") != NULL ||
        strstr(output, "BLOCKED:") == NULL || error[0] != '\0') {
        fprintf(stderr, "unexpected inspection result:\n%s%s", output, error);
        failures++;
    }
    free(output); free(error);
    if (nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0) failures++;
    return failures == 0 ? 0 : 1;
}
